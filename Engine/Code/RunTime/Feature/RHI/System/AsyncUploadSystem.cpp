#include "AsyncUploadSystem.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <Log/ILogSystem.h>
#include <Math/Bit.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/CommandQueueContext.h>
#include <RHI/Command/CopyItem.h>

namespace Spark::RHI
{
    namespace
    {
        HardwareQueueClass ResolveHomeQueue(HardwareQueueClassMask mask)
        {
            if (mask == HardwareQueueClassMask::Compute)
            {
                return HardwareQueueClass::Compute;
            }
            if (mask == HardwareQueueClassMask::Copy)
            {
                return HardwareQueueClass::Copy;
            }
            return HardwareQueueClass::Graphics;
        }

        // Returns true if the upload into `handle` has to be dropped, with the error logged.
        bool ValidateBufferUpload(
            RHIHandle handle,
            HardwareQueueClassMask mask,
            bool exclusive,
            const ResourceState& curState)
        {
            if (mask == HardwareQueueClassMask::None)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} target buffer has empty "
                          "m_sharedQueueMask; declare at least one consumer queue.",
                          static_cast<uint32_t>(handle));
                return true;
            }
            if (exclusive && ResolveHomeQueue(mask) == HardwareQueueClass::Copy)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} declares EXCLUSIVE Copy-only "
                          "m_sharedQueueMask, which has no consumer. Set at least one "
                          "non-Copy queue.", static_cast<uint32_t>(handle));
                return true;
            }
            if (exclusive && curState.m_access != AccessFlags::None)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} is EXCLUSIVE and already in use "
                          "(access=0x{:x}). Exclusive resources cannot be re-uploaded -- "
                          "set m_sharedQueueMask to multi-bit (CONCURRENT) for "
                          "re-uploadable resources.",
                          static_cast<uint32_t>(handle),
                          static_cast<uint32_t>(curState.m_access));
                return true;
            }
            return false;
        }

        // Returns true if the upload into `range` of `image` has to be dropped, with the error logged.
        bool ValidateImageUpload(RHIHandle handle, const Image& image, const ImageSubresourceRange& range)
        {
            const HardwareQueueClassMask mask = image.GetDescriptor().m_sharedQueueMask;
            if (mask == HardwareQueueClassMask::None)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} target image has empty "
                          "m_sharedQueueMask; declare at least one consumer queue.",
                          static_cast<uint32_t>(handle));
                return true;
            }
            // Its layout would be changed on one of its queues with the others not ordered
            // behind that.
            if (!IsExclusiveQueueMask(mask))
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} target image is shared by several queues "
                          "(m_sharedQueueMask=0x{:x}); uploading into one is not supported.",
                          static_cast<uint32_t>(handle), static_cast<uint32_t>(mask));
                return true;
            }
            if (ResolveHomeQueue(mask) == HardwareQueueClass::Copy)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} declares EXCLUSIVE Copy-only "
                          "m_sharedQueueMask, which has no consumer. Set at least one "
                          "non-Copy queue.", static_cast<uint32_t>(handle));
                return true;
            }

            ImageSubresourceStates::SpanList spans;
            image.GetSubresourceStates().GetSpans(range, spans);
            for (const ImageSubresourceSpan& span : spans)
            {
                if (span.m_state.m_access != AccessFlags::None)
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} has subresources already in use "
                              "(access=0x{:x}) among those the upload writes. Only new "
                              "subresources can be uploaded into.",
                              static_cast<uint32_t>(handle),
                              static_cast<uint32_t>(span.m_state.m_access));
                    return true;
                }
            }
            return false;
        }

        // What an upload hands over to the queue that owns its target: the Copy queue records
        // it to release, the owner to acquire, the same barrier both times. The buffer is
        // left readable in every way its bind flags allow; with none it stays as the copy
        // left it, for the render graph to take from there.
        BufferBarrier MakeBufferHandoff(Buffer& buffer)
        {
            const AccessFlags reads = GetReadOnlyAccess(buffer);
            const bool        read  = reads != AccessFlags::None;

            BufferBarrier barrier;
            barrier.m_buffer    = &buffer;
            barrier.m_srcAccess = AccessFlags::TransferWrite;
            barrier.m_srcStage  = AttachmentStage::Copy;
            barrier.m_srcQueue  = HardwareQueueClass::Copy;
            barrier.m_dstAccess = read ? reads : AccessFlags::TransferWrite;
            barrier.m_dstStage  = read ? AttachmentStage::Any : AttachmentStage::Copy;
            barrier.m_dstQueue  = ResolveHomeQueue(buffer.GetDescriptor().m_sharedQueueMask);
            return barrier;
        }

        // Image counterpart of MakeBufferHandoff, over the subresources the upload wrote.
        // Sampling is the one use of an image no pass declares, so an image that can be
        // sampled is left ready for it; any other keeps the copy's layout for the render
        // graph to take from there.
        ImageBarrier MakeImageHandoff(Image& image, const ImageSubresourceRange& range)
        {
            const ImageDescriptor& descriptor = image.GetDescriptor();
            const bool sampled = CheckBitsAny(descriptor.m_bindFlags, ImageBindFlags::ShaderRead);

            ImageBarrier barrier;
            barrier.m_image     = &image;
            barrier.m_range     = range;
            barrier.m_srcAccess = AccessFlags::TransferWrite;
            barrier.m_srcStage  = AttachmentStage::Copy;
            barrier.m_srcQueue  = HardwareQueueClass::Copy;
            barrier.m_dstAccess = sampled ? AccessFlags::ShaderSampledRead : AccessFlags::TransferWrite;
            barrier.m_dstStage  = sampled ? AttachmentStage::Any : AttachmentStage::Copy;
            barrier.m_dstQueue  = ResolveHomeQueue(descriptor.m_sharedQueueMask);
            return barrier;
        }

        // Puts on a request whose copy is over the barrier that takes its target back, and
        // moves it to awaiting that. False when there is none to take: a buffer several
        // queues share, or a target that is gone.
        bool AwaitAcquire(RHIContext& ctx, RHIEventContext& events, RHIEvent request)
        {
            Resource& resource = *events.Get<UploadBatch>(request).m_resource;
            if (const auto* bufferUpload = events.TryGet<BufferUploadRequest>(request))
            {
                Buffer& buffer = static_cast<Buffer&>(resource);
                if (!ctx.Valid(bufferUpload->m_target) || !IsExclusiveQueueMask(buffer.GetDescriptor().m_sharedQueueMask))
                {
                    return false;
                }
                events.Add<BufferBarrier>(request, MakeBufferHandoff(buffer));
            }
            else
            {
                const ImageUploadRequest& imageUpload = events.Get<ImageUploadRequest>(request);
                if (!ctx.Valid(imageUpload.m_target))
                {
                    return false;
                }
                events.Add<ImageBarrier>(request, MakeImageHandoff(static_cast<Image&>(resource), imageUpload.m_range));
            }
            events.Remove<UploadingTag>(request);
            events.Add<UploadAcquirePendingTag>(request);
            return true;
        }
    }

    void ExecuteUploadAcquires(Device& device, CommandQueueContext& queues)
    {
        auto& ctx    = *RHIExecuteContext::Current();
        auto& events = *RHIEventExecuteContext::Current();

        eastl::array<CommandList*, HardwareQueueClassCount> recording {};
        auto open = [&](HardwareQueueClass queueClass)
        {
            CommandList*& commandList = recording[static_cast<uint32_t>(queueClass)];
            if (!commandList)
            {
                commandList = Service<Factory>::Get()->CreateCommandList(device, queueClass);
                commandList->Open();
            }
            return commandList;
        };

        eastl::vector<RHIEvent> acquired;
        events.GetView<UploadAcquirePendingTag, BufferBarrier>().each([&](RHIEvent request, const BufferBarrier& barrier)
        {
            open(barrier.m_dstQueue)->QueueBarrier(barrier);
            acquired.push_back(request);
        });
        events.GetView<UploadAcquirePendingTag, ImageBarrier>().each([&](RHIEvent request, const ImageBarrier& barrier)
        {
            open(barrier.m_dstQueue)->QueueBarrier(barrier);
            acquired.push_back(request);
        });

        for (uint32_t queueIndex = 0; queueIndex < HardwareQueueClassCount; ++queueIndex)
        {
            CommandList* commandList = recording[queueIndex];
            if (!commandList)
            {
                continue;
            }
            commandList->FlushBarriers();
            commandList->Close();
            queues.GetCommandQueue(static_cast<HardwareQueueClass>(queueIndex)).ExecuteCommands({ &commandList, 1 });
        }

        for (RHIEvent request : acquired)
        {
            FinishUploadRequest(ctx, events, request);
        }
    }

    void AsyncUploadSystem::InitInternal()
    {
        auto* rhi = Service<RHIInterface>::Get();
        auto* factory = rhi->GetRHIFactory();
        auto* device = rhi->GetDevice();

        // Copy queue
        {
            CommandQueueDescriptor queueDesc;
            queueDesc.m_hardwareQueueClass = HardwareQueueClass::Copy;
            m_copyQueue = factory->CreateCommandQueue();
            if (m_copyQueue->Init(*device, queueDesc) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init copy queue.");
                return;
            }
        }

        // Staging pool — UPLOAD heap, CPU-writable, source of copy commands
        {
            BufferPoolDescriptor poolDesc;
            poolDesc.m_heapMemoryLevel = HeapMemoryLevel::Host;
            poolDesc.m_hostMemoryAccess = HostMemoryAccess::Write;
            poolDesc.m_bindFlags = BufferBindFlags::CopyRead;
            poolDesc.m_sharedQueueMask = HardwareQueueClassMask::All;
            m_stagingPool = factory->CreateBufferPool();
            if (m_stagingPool->Init(*device, poolDesc) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init staging pool.");
                return;
            }
        }

        // Upload fence — external contract, one Signal per batch.
        // Signaled init keeps pending == completed == 0 before the first batch,
        // so a no-op shutdown's FlushUploadPackets doesn't block on a value that
        // will never be signalled.
        {
            m_uploadFence = factory->CreateFence();
            if (m_uploadFence->Init(*device, FenceState::Signaled) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init upload fence.");
                return;
            }
        }

        // Pre-allocate staging packets
        const uint32_t frameCount = device->GetDescriptor().m_frameCountMax;
        m_packets.resize(frameCount);
        for (uint32_t i = 0; i < frameCount; ++i)
        {
            auto& packet = m_packets[i];

            BufferDescriptor stagingDesc;
            stagingDesc.m_bindFlags = BufferBindFlags::CopyRead;
            stagingDesc.m_byteCount = m_descriptor.m_stagingSizeInBytes;

            packet.m_stagingBuffer = factory->CreateBuffer();
            if (m_stagingPool->InitBuffer(BufferInitRequest{*packet.m_stagingBuffer, stagingDesc}) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init staging buffer.");
                return;
            }

            BufferMapResponse mapResponse;
            m_stagingPool->MapBuffer(
                BufferMapRequest{*packet.m_stagingBuffer, 0, m_descriptor.m_stagingSizeInBytes},
                mapResponse);
            packet.m_mappedPtr = static_cast<uint8_t*>(mapResponse.m_data);

            packet.m_commandRecorder = factory->CreateCommandRecorder();
            CommandRecorderDescriptor recorderDesc;
            recorderDesc.m_queue = HardwareQueueClass::Copy;
            if (packet.m_commandRecorder->Init(*device, recorderDesc) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init command recorder.");
                return;
            }

            packet.m_fence = factory->CreateFence();
            if (packet.m_fence->Init(*device, FenceState::Reset) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init packet fence {}.", i);
                return;
            }
        }

        // Start upload thread
        m_running.store(true);
        m_uploadThread = std::thread(&AsyncUploadSystem::UploadThreadMain, this);

        FrameEventBus::Handler::BusConnect();
    }

    void AsyncUploadSystem::ShutdownInternal()
    {
        FrameEventBus::Handler::BusDisconnect();

        m_running.store(false);
        m_cv.notify_one();

        if (m_uploadThread.joinable())
        {
            m_uploadThread.join();
        }

        // Drain remaining batches
        while (!m_pendingBatches.empty())
        {
            ProcessBatch(m_pendingBatches.front());
            m_pendingBatches.pop_front();
        }

        FlushUploadPackets();

        // The requests hold their targets; let go before the pools those came from shut down.
        if (auto* events = RHIEventExecuteContext::Current())
        {
            eastl::vector<RHIEvent> requests;
            events->GetView<BufferUploadRequest>().each(
                [&](RHIEvent request, const BufferUploadRequest&) { requests.push_back(request); });
            events->GetView<ImageUploadRequest>().each(
                [&](RHIEvent request, const ImageUploadRequest&) { requests.push_back(request); });
            events->DestoryEntity(requests.begin(), requests.end());
        }

        for (auto& packet : m_packets)
        {
            m_stagingPool->UnmapBuffer(*packet.m_stagingBuffer);
        }
        m_packets.clear();

    }

    void AsyncUploadSystem::OnFrameBegin()
    {
        auto& ctx    = *RHIExecuteContext::Current();
        auto& events = *RHIEventExecuteContext::Current();

        FinishUploads(ctx, events);
        SubmitBatch(ctx, events);
    }

    void AsyncUploadSystem::FinishUploads(RHIContext& ctx, RHIEventContext& events)
    {
        const uint64_t completed = m_uploadFence->GetCompletedValue();

        eastl::vector<RHIEvent> copied;
        events.GetView<UploadingTag, UploadBatch>().each([&](RHIEvent request, const UploadBatch& batch)
        {
            if (batch.m_fenceValue <= completed)
            {
                copied.push_back(request);
            }
        });
        for (RHIEvent request : copied)
        {
            if (!AwaitAcquire(ctx, events, request))
            {
                FinishUploadRequest(ctx, events, request);
            }
        }
    }

    void AsyncUploadSystem::FlushUploadPackets()
    {
        const uint64_t pending = m_uploadFence->GetPendingValue();
        if (pending > m_uploadFence->GetCompletedValue())
        {
            m_uploadFence->WaitOnCpu();
        }
    }

    void AsyncUploadSystem::SubmitBatch(RHIContext& ctx, RHIEventContext& events)
    {
        auto bufferView = events.GetView<UploadRequestedTag, BufferUploadRequest>();
        auto imageView  = events.GetView<UploadRequestedTag, ImageUploadRequest>();

        Batch batch;
        batch.m_bufferUploads.reserve(bufferView.size_hint());
        batch.m_imageUploads.reserve(imageView.size_hint());
        batch.m_imagePreBarriers.reserve(imageView.size_hint());
        batch.m_bufferReleaseBarriers.reserve(bufferView.size_hint());
        batch.m_imageReleaseBarriers.reserve(imageView.size_hint());

        struct Taken
        {
            RHIEvent      m_request;
            Ptr<Resource> m_resource;
        };
        eastl::vector<Taken> taken;
        taken.reserve(bufferView.size_hint() + imageView.size_hint());

        // Requests that cannot be uploaded. Over once both views are walked.
        eastl::vector<RHIEvent> dropped;

        bufferView.each([&](RHIEvent request, const BufferUploadRequest& pending)
        {
            const RHIHandle handle = pending.m_target;
            if (!ctx.Valid(handle))
            {
                // The target went before its upload was taken.
                dropped.push_back(request);
                return;
            }

            auto* owning = ctx.TryGet<Components::Buffer>(handle);
            if (!owning || !owning->m_buffer)
            {
                // Materialization still in flight — PendingBufferInit is present so
                // RHIResourceSystem will create the buffer in a future frame.
                if (ctx.Has<PendingBufferInit>(handle))
                {
                    return;
                }
                // Host-visible per-frame buffers must not go through staging — they're
                // CPU-mapped and the caller should write to them directly via Map().
                if (ctx.TryGet<Components::BufferPerFrame>(handle))
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} has a buffer upload requested "
                              "but is host-visible (BufferPerFrame). Write via Map() "
                              "instead of staging upload.",
                              static_cast<uint32_t>(handle));
                }
                else
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} has a buffer upload requested "
                              "but has no materialized Buffer and no PendingBufferInit.",
                              static_cast<uint32_t>(handle));
                }
                dropped.push_back(request);
                return;
            }

            Buffer* target = owning->m_buffer.get();
            const auto& desc  = target->GetDescriptor();
            const auto  mask  = desc.m_sharedQueueMask;
            const bool  exclusive = IsExclusiveQueueMask(mask);
            const ResourceState curState = target->GetResourceState();

            if (ValidateBufferUpload(handle, mask, exclusive, curState))
            {
                dropped.push_back(request);
                return;
            }

            if (auto* sync = ctx.TryGet<PendingSync>(handle))
            {
                if (sync->m_fence->GetCompletedValue() < sync->m_fenceValue)
                {
                    // Skip the buffer that are in use
                    return;
                } 
            }

            BufferUpload upload;
            upload.m_data              = pending.m_data;
            upload.m_dataSize          = pending.m_dataSize;
            upload.m_targetBuffer      = target;
            upload.m_destinationOffset = pending.m_destinationOffset;
            batch.m_bufferUploads.push_back(upload);

            // An exclusive buffer is released to its queue. A shared one has no owner and a
            // buffer no layout: the fence is all that orders this write against the other
            // queues. Neither takes a barrier before the copy: the Copy queue needs no
            // transfer to write a buffer whose contents it does not keep.
            if (exclusive)
            {
                batch.m_bufferReleaseBarriers.push_back(MakeBufferHandoff(*target));
            }

            taken.push_back(Taken{ request, Ptr<Resource>(target) });
        });

        imageView.each([&](RHIEvent request, const ImageUploadRequest& pending)
        {
            const RHIHandle handle = pending.m_target;
            if (!ctx.Valid(handle))
            {
                dropped.push_back(request);
                return;
            }

            auto* owning = ctx.TryGet<Components::Image>(handle);
            if (!owning || !owning->m_image)
            {
                // Materialization still in flight.
                if (ctx.Has<PendingImageInit>(handle))
                {
                    return;
                }
                if (ctx.TryGet<Components::ImagePerFrame>(handle))
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} has an image upload requested "
                              "but is host-visible (ImagePerFrame). Staging upload is "
                              "not supported for per-frame images.",
                              static_cast<uint32_t>(handle));
                }
                else
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} has an image upload requested "
                              "but has no materialized Image and no PendingImageInit.",
                              static_cast<uint32_t>(handle));
                }
                dropped.push_back(request);
                return;
            }

            Image* target = owning->m_image.get();
            if (ValidateImageUpload(handle, *target, pending.m_range))
            {
                dropped.push_back(request);
                return;
            }

            if (auto* sync = ctx.TryGet<PendingSync>(handle))
            {
                if (sync->m_fence->GetCompletedValue() < sync->m_fenceValue)
                {
                    // Skip the image that are in use
                    return;
                } 
            }

            ImageUpload upload;
            upload.m_data              = pending.m_data;
            upload.m_dataSize          = pending.m_dataSize;
            upload.m_targetImage       = target;
            upload.m_range             = pending.m_range;
            upload.m_destinationOrigin = pending.m_destinationOrigin;
            upload.m_sourceFormat      = pending.m_sourceFormat;
            batch.m_imageUploads.push_back(upload);

            // The subresources are new, so nothing of theirs is kept: the Copy queue turns
            // them into a copy destination itself, with no queue to take them from.
            ImageBarrier pre;
            pre.m_image     = target;
            pre.m_range     = pending.m_range;
            pre.m_srcAccess = AccessFlags::Undefined;
            pre.m_srcStage  = AttachmentStage::Uninitialized;
            pre.m_srcQueue  = HardwareQueueClass::Copy;
            pre.m_dstAccess = AccessFlags::TransferWrite;
            pre.m_dstStage  = AttachmentStage::Copy;
            pre.m_dstQueue  = HardwareQueueClass::Copy;
            batch.m_imagePreBarriers.push_back(pre);

            batch.m_imageReleaseBarriers.push_back(MakeImageHandoff(*target, pending.m_range));

            taken.push_back(Taken{ request, Ptr<Resource>(target) });
        });

        for (RHIEvent request : dropped)
        {
            FinishUploadRequest(ctx, events, request);
        }

        if (batch.m_bufferUploads.empty() && batch.m_imageUploads.empty())
        {
            return;
        }

        // Allocate the batch's fence value from a main-thread-private counter.
        // The actual fence pending value is updated later on the upload thread
        // when ProcessBatch's CommandQueue::Signal runs, keeping m_uploadFence's
        // pending value single-writer.
        batch.m_fenceValue = ++m_batchFenceValue;

        for (Taken& entry : taken)
        {
            events.Remove<UploadRequestedTag>(entry.m_request);
            events.Add<UploadingTag>(entry.m_request);
            events.Add<UploadBatch>(entry.m_request,
                UploadBatch{ batch.m_fenceValue, eastl::move(entry.m_resource) });
        }

        {
            std::lock_guard lk(m_mutex);
            m_pendingBatches.push_back(eastl::move(batch));
        }
        m_cv.notify_one();
    }

    void AsyncUploadSystem::UploadThreadMain()
    {
        while (m_running.load())
        {
            Batch batch;
            {
                std::unique_lock lk(m_mutex);
                m_cv.wait(lk, [&] { return !m_running.load() || !m_pendingBatches.empty(); });
                if (!m_running.load())
                {
                    break;
                }
                batch = eastl::move(m_pendingBatches.front());
                m_pendingBatches.pop_front();
            }
            ProcessBatch(batch);
        }
    }

    void AsyncUploadSystem::ProcessBatch(Batch& batch)
    {
        auto* packet = &m_packets[m_currentPacketIndex];

        // The cmdList is left in recording state by CommandRecorder::Init, so on
        // the very first use of each packet (m_fenceValue == 0) we must not Reset
        // the allocator — that would fail with "command allocator cannot be reset
        // because a command list is currently being recorded". On reuse, the
        // previous ProcessBatch left the cmdList Closed; we wait for the GPU to
        // drain it, then Reset.
        if (packet->m_fenceValue > 0)
        {
            if (packet->m_fenceValue > packet->m_fence->GetCompletedValue())
            {
                packet->m_fence->WaitOnCpu();
            }
            packet->m_commandRecorder->Reset();
        }
        packet->m_offset = 0;

        CommandList* cmdList = packet->m_commandRecorder->GetCommandList();

        // Mid-batch flush: staging packet is full, retire it and rotate to the next.
        // Each packet owns its own fence — Signal/Wait are naturally scoped to
        // the packet's previous use, no cross-packet over-wait.
        auto SubmitFramePacket = [&]()
        {
            Fence* const fence = packet->m_fence.get();

            cmdList->Close();
            m_copyQueue->ExecuteCommands({ &cmdList, 1 });

            packet->m_fenceValue = fence->Increment();
            m_copyQueue->Signal(*fence);

            m_currentPacketIndex = (m_currentPacketIndex + 1) % m_packets.size();
            packet = &m_packets[m_currentPacketIndex];

            // Same first-use-vs-reuse split as the top of ProcessBatch:
            // freshly-Init'd packets are already in recording state.
            if (packet->m_fenceValue > 0)
            {
                if (packet->m_fenceValue > packet->m_fence->GetCompletedValue())
                {
                    packet->m_fence->WaitOnCpu();
                }
                packet->m_commandRecorder->Reset();
            }
            packet->m_offset = 0;

            cmdList = packet->m_commandRecorder->GetCommandList();
        };

        // The one place this thread writes a resource's tracked state: the subresources an
        // image upload turns into a copy destination.
        for (const auto& barrier : batch.m_imagePreBarriers)
        {
            cmdList->QueueBarrier(barrier);
        }
        cmdList->FlushBarriers();

        // Process buffer uploads
        for (const auto& upload : batch.m_bufferUploads)
        {
            size_t srcOffset = 0;
            size_t dstOffset = upload.m_destinationOffset;
            size_t pendingByteCount = upload.m_dataSize;
            const auto* src = static_cast<const uint8_t*>(upload.m_data);

            while (pendingByteCount > 0)
            {
                if (packet->m_offset >= m_descriptor.m_stagingSizeInBytes)
                {
                    SubmitFramePacket();
                }

                const size_t bytesToCopy = eastl::min(pendingByteCount, m_descriptor.m_stagingSizeInBytes - packet->m_offset);
                memcpy(packet->m_mappedPtr + packet->m_offset, src + srcOffset, bytesToCopy);

                CopyBufferDescriptor copyDesc;
                copyDesc.m_sourceBuffer      = packet->m_stagingBuffer.get();
                copyDesc.m_sourceOffset      = packet->m_offset;
                copyDesc.m_destinationBuffer = upload.m_targetBuffer;
                copyDesc.m_destinationOffset = dstOffset;
                copyDesc.m_size              = bytesToCopy;

                cmdList->Submit(CopyItem{ copyDesc });

                packet->m_offset += static_cast<uint32_t>(bytesToCopy);
                pendingByteCount -= bytesToCopy;
                srcOffset += bytesToCopy;
                dstOffset += bytesToCopy;
            }
        }

        // Process image uploads
        //
        // Scratch for the per-subresource layouts, reused across uploads so the
        // (potentially large) allocation happens at most once per batch.
        eastl::vector<ImageSubresourceLayout> layouts;

        for (const auto& upload : batch.m_imageUploads)
        {
            const auto& imageDesc = upload.m_targetImage->GetDescriptor();
            const uint32_t mipLevels = imageDesc.m_mipLevels;

            layouts.assign(
                static_cast<size_t>(mipLevels) * imageDesc.m_arraySize,
                ImageSubresourceLayout{});
            upload.m_targetImage->GetSubresourceLayouts(upload.m_range, layouts.data(), nullptr);
            const uint32_t offsetAlignment = GetImageCopyOffsetAlignment(
                imageDesc.m_format, upload.m_targetImage->GetDevice().GetLimits());

            const uint8_t* srcData = static_cast<const uint8_t*>(upload.m_data);

            for (uint16_t arraySlice = upload.m_range.m_arraySliceMin; arraySlice <= upload.m_range.m_arraySliceMax; ++arraySlice)
            {
                for (uint16_t mipSlice = upload.m_range.m_mipSliceMin; mipSlice <= upload.m_range.m_mipSliceMax; ++mipSlice)
                {
                    const uint32_t subresourceIndex = GetImageSubresourceIndex(mipSlice, arraySlice, mipLevels);
                    const ImageSubresourceLayout& layout = layouts[subresourceIndex];

                    // GPU-side layout (rows already aligned by GetSubresourceLayouts to the
                    // device's copy row pitch). Used for staging-buffer writes
                    // and the CopyBufferToImage descriptor.
                    const uint32_t dstRowPitch  = layout.m_bytesPerRow;
                    const uint32_t numRows      = layout.m_rowCount;
                    const uint32_t blockHeight  = layout.m_blockElementHeight; // texel rows per copy row (1, or 4 for BC)

                    // CPU-side layout (unaligned). ImageAsset stores mips tightly packed;
                    // we must not read past the end of each source row.
                    RHI::ImageSubresourceLayout srcLayout =
                        RHI::GetImageSubresourceLayout(imageDesc, RHI::ImageSubresource(mipSlice, arraySlice));
                    const uint32_t srcRowPitch     = srcLayout.m_bytesPerRow;
                    const uint32_t srcBytesPerImage = srcLayout.m_bytesPerImage;

                    // Split the subresource into horizontal row-bands, each sized to fit
                    // the current staging packet; a band that fills the packet rotates to
                    // a fresh one. When the whole subresource fits this is a single band ==
                    // the previous behaviour. Chunking only engages for subresources larger
                    // than staging, which are always full-block mips, so intermediate band
                    // boundaries land on block rows (BC-safe). The copy needs the source
                    // offset aligned to offsetAlignment (512 on DX12); AlignUpNPOT handles
                    // the arbitrary offset earlier uploads leave.
                    if (layout.m_size.m_depth != 1)
                    {
                        // The subresource loop copies one 2D slice; 3D depth-slicing is not
                        // handled. No 3D upload path exists today — guard rather than corrupt.
                        LOG_ERROR("[AsyncUploadSystem] 3D chunked image upload is not supported "
                                  "(depth={}).", layout.m_size.m_depth);
                        srcData += srcBytesPerImage;
                        continue;
                    }
                    if (dstRowPitch > m_descriptor.m_stagingSizeInBytes)
                    {
                        LOG_ERROR("[AsyncUploadSystem] Row pitch {} exceeds staging size {}; "
                                  "cannot upload this subresource.",
                                  dstRowPitch, m_descriptor.m_stagingSizeInBytes);
                        srcData += srcBytesPerImage;
                        continue;
                    }

                    uint32_t rowsCopied = 0;
                    while (rowsCopied < numRows)
                    {
                        uint32_t alignedOffset = AlignUpNPOT(packet->m_offset, offsetAlignment);
                        uint32_t rowsFit = alignedOffset < m_descriptor.m_stagingSizeInBytes
                            ? static_cast<uint32_t>((m_descriptor.m_stagingSizeInBytes - alignedOffset) / dstRowPitch)
                            : 0;
                        if (rowsFit == 0)
                        {
                            SubmitFramePacket(); // packet full — retire it and rotate
                            alignedOffset = 0;   // a fresh packet is aligned at offset 0
                            rowsFit = static_cast<uint32_t>(m_descriptor.m_stagingSizeInBytes / dstRowPitch); // >= 1 (guarded above)
                        }
                        packet->m_offset = alignedOffset;

                        const uint32_t bandRows = eastl::min(numRows - rowsCopied, rowsFit);

                        // Tightly-packed source rows -> aligned staging rows.
                        const uint8_t* srcRow = srcData + static_cast<size_t>(rowsCopied) * srcRowPitch;
                        uint8_t*       dstRow = packet->m_mappedPtr + packet->m_offset;
                        for (uint32_t row = 0; row < bandRows; ++row)
                        {
                            memcpy(dstRow, srcRow, srcRowPitch);
                            srcRow += srcRowPitch;
                            dstRow += dstRowPitch;
                        }

                        // Destination sub-region: a horizontal band of this subresource.
                        // The final band clamps to the true texel height (edge of the mip).
                        const uint32_t texelTop    = rowsCopied * blockHeight;
                        const uint32_t texelHeight = eastl::min(bandRows * blockHeight, layout.m_size.m_height - texelTop);

                        CopyBufferToImageDescriptor copyDesc;
                        copyDesc.m_sourceBuffer           = packet->m_stagingBuffer.get();
                        copyDesc.m_sourceOffset           = packet->m_offset;
                        copyDesc.m_sourceBytesPerRow      = dstRowPitch;
                        copyDesc.m_sourceBytesPerImage    = bandRows * dstRowPitch;
                        copyDesc.m_sourceFormat           = upload.m_sourceFormat;
                        copyDesc.m_sourceSize             = RHI::Size(layout.m_size.m_width, texelHeight, 1);
                        copyDesc.m_destinationImage       = upload.m_targetImage;
                        copyDesc.m_destinationSubresource = ImageSubresource(mipSlice, arraySlice);
                        copyDesc.m_destinationOrigin      = RHI::Origin(
                            upload.m_destinationOrigin.m_left,
                            upload.m_destinationOrigin.m_top + texelTop,
                            upload.m_destinationOrigin.m_front);

                        cmdList->Submit(CopyItem{ copyDesc });

                        packet->m_offset += bandRows * dstRowPitch;
                        rowsCopied += bandRows;
                    }
                    srcData += srcBytesPerImage;
                }
            }
        }

        // Release barriers: the release half of the handoff to the queue that owns each
        // target. The acquire half is the same barrier, put on the request once this batch's
        // fence is reached (FinishUploads).
        for (const auto& barrier : batch.m_bufferReleaseBarriers)
        {
            cmdList->QueueBarrier(barrier);
        }
        for (const auto& barrier : batch.m_imageReleaseBarriers)
        {
            cmdList->QueueBarrier(barrier);
        }
        cmdList->FlushBarriers();

        // Batch end: single Signal on m_uploadFence carries the external contract.
        // Also stamp the current packet's fence so future rotations can tell the
        // GPU is done with this packet's data.
        cmdList->Close();
        m_copyQueue->ExecuteCommands({ &cmdList, 1 });

        packet->m_fenceValue = packet->m_fence->Increment();
        m_copyQueue->Signal(*packet->m_fence);

        m_copyQueue->Signal(*m_uploadFence, batch.m_fenceValue);

        // Advance to the next packet so the next batch reuses a different
        // command allocator + staging range — gives us m_packets.size() batches
        // of in-flight pipelining before the top-of-ProcessBatch wait actually
        // has to block.
        m_currentPacketIndex = (m_currentPacketIndex + 1) % m_packets.size();
    }

}
