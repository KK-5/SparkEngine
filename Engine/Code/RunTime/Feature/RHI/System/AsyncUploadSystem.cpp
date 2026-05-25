#include "AsyncUploadSystem.h"

#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/CopyItem.h>

namespace Spark::RHI
{
    namespace
    {
        // Single bit → EXCLUSIVE; multi bit → CONCURRENT.
        bool IsExclusive(HardwareQueueClassMask mask)
        {
            return CountBitsSet(static_cast<uint32_t>(mask)) == 1;
        }
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

        // Returns true if the entity should be skipped (validation failed).
        // On failure, logs the error and removes UploadPendingTag + PendingBufferUpload.
        bool ValidateBufferUpload(
            RHIContext& ctx,
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
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return true;
            }
            if (exclusive && ResolveHomeQueue(mask) == HardwareQueueClass::Copy)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} declares EXCLUSIVE Copy-only "
                          "m_sharedQueueMask, which has no consumer. Set at least one "
                          "non-Copy queue.", static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return true;
            }
            if (exclusive && curState.m_usage != AttachmentUsage::Uninitialized)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} is EXCLUSIVE and already in use "
                          "(usage={}). Exclusive resources cannot be re-uploaded — "
                          "set m_sharedQueueMask to multi-bit (CONCURRENT) for "
                          "re-uploadable resources.",
                          static_cast<uint32_t>(handle),
                          static_cast<uint32_t>(curState.m_usage));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return true;
            }
            return false;
        }

        // Returns true if the entity should be skipped (validation failed).
        // On failure, logs the error and removes UploadPendingTag + PendingImageUpload.
        bool ValidateImageUpload(
            RHIContext& ctx,
            RHIHandle handle,
            HardwareQueueClassMask mask,
            bool exclusive,
            const ResourceState& curState)
        {
            if (mask == HardwareQueueClassMask::None)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} target image has empty "
                          "m_sharedQueueMask; declare at least one consumer queue.",
                          static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return true;
            }
            if (exclusive && ResolveHomeQueue(mask) == HardwareQueueClass::Copy)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} declares EXCLUSIVE Copy-only "
                          "m_sharedQueueMask, which has no consumer. Set at least one "
                          "non-Copy queue.", static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return true;
            }
            if (exclusive && curState.m_usage != AttachmentUsage::Uninitialized)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} is EXCLUSIVE and already in use "
                          "(usage={}). Exclusive resources cannot be re-uploaded — "
                          "set m_sharedQueueMask to multi-bit (CONCURRENT) for "
                          "re-uploadable resources.",
                          static_cast<uint32_t>(handle),
                          static_cast<uint32_t>(curState.m_usage));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return true;
            }
            return false;
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

        for (auto& packet : m_packets)
        {
            m_stagingPool->UnmapBuffer(*packet.m_stagingBuffer);
        }
        m_packets.clear();

    }

    void AsyncUploadSystem::OnFrameBegin()
    {
        auto& ctx = *RHIExecuteContext::Current();

        // Submitted upload entities carry PendingSync with the upload fence.
        // The RG barrier compiler consumes PendingSync on first cross-queue
        // touch → PassExternalFenceWaits → executer emits queue.Wait before
        // the acquire barrier. No CPU-side poll step needed.
        SubmitBatch(ctx);
    }

    void AsyncUploadSystem::FlushUploadPackets()
    {
        const uint64_t pending = m_uploadFence->GetPendingValue();
        if (pending > m_uploadFence->GetCompletedValue())
        {
            m_uploadFence->WaitOnCpu();
        }
    }

    void AsyncUploadSystem::SubmitBatch(RHIContext& ctx)
    {
        auto bufferView = ctx.GetView<UploadPendingTag, PendingBufferUpload>();
        auto imageView  = ctx.GetView<UploadPendingTag, PendingImageUpload>();

        Batch batch;
        batch.m_bufferUploads.reserve(bufferView.size_hint());
        batch.m_imageUploads.reserve(imageView.size_hint());
        batch.m_bufferReleaseBarriers.reserve(bufferView.size_hint());
        batch.m_imageReleaseBarriers.reserve(imageView.size_hint());

        eastl::vector<RHIHandle> touchedEntities;
        touchedEntities.reserve(bufferView.size_hint() + imageView.size_hint());

        bufferView.each([&](RHIHandle handle, const PendingBufferUpload& pending)
        {
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
                    LOG_ERROR("[AsyncUploadSystem] Entity {} carries PendingBufferUpload "
                              "but is host-visible (BufferPerFrame). Write via Map() "
                              "instead of staging upload.",
                              static_cast<uint32_t>(handle));
                }
                else
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} carries PendingBufferUpload "
                              "but has no materialized Buffer and no PendingBufferInit.",
                              static_cast<uint32_t>(handle));
                }
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return;
            }

            Buffer* target = owning->m_buffer.get();
            const auto& desc  = target->GetDescriptor();
            const auto  mask  = desc.m_sharedQueueMask;
            const bool  exclusive = IsExclusive(mask);
            const ResourceState curState = target->GetResourceState();

            if (ValidateBufferUpload(ctx, handle, mask, exclusive, curState))
                return;

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

            // Release barrier:
            //  - EXCLUSIVE:  Copy/Write → COMMON, srcQueue=Copy, dstQueue=homeQueue
            //                (real Vulkan QFOT release half; DX12 lands at COMMON)
            //  - CONCURRENT: Copy/Write → COMMON, intra-Copy (no QFOT in Vulkan,
            //                Copy→COMMON in DX12; consumer pulls from there)
            BufferBarrier barrier;
            barrier.m_buffer    = target;
            barrier.m_srcUsage  = AttachmentUsage::Copy;
            barrier.m_srcAccess = AttachmentAccess::Write;
            barrier.m_dstUsage  = AttachmentUsage::Uninitialized;
            barrier.m_dstAccess = AttachmentAccess::Unknown;
            barrier.m_srcStage  = AttachmentStage::Copy;
            barrier.m_dstStage  = AttachmentStage::Any;
            barrier.m_srcQueue  = HardwareQueueClass::Copy;
            barrier.m_dstQueue  = exclusive ? ResolveHomeQueue(mask) : HardwareQueueClass::Copy;
            batch.m_bufferReleaseBarriers.push_back(barrier);

            touchedEntities.push_back(handle);

            ctx.Remove<UploadPendingTag>(handle);
            ctx.Remove<PendingBufferUpload>(handle);
        });

        imageView.each([&](RHIHandle handle, const PendingImageUpload& pending)
        {
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
                    LOG_ERROR("[AsyncUploadSystem] Entity {} carries PendingImageUpload "
                              "but is host-visible (ImagePerFrame). Staging upload is "
                              "not supported for per-frame images.",
                              static_cast<uint32_t>(handle));
                }
                else
                {
                    LOG_ERROR("[AsyncUploadSystem] Entity {} carries PendingImageUpload "
                              "but has no materialized Image and no PendingImageInit.",
                              static_cast<uint32_t>(handle));
                }
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return;
            }

            Image* target = owning->m_image.get();
            const auto& desc  = target->GetDescriptor();
            const auto  mask  = desc.m_sharedQueueMask;
            const bool  exclusive = IsExclusive(mask);
            const ResourceState curState = target->GetResourceState();

            if (ValidateImageUpload(ctx, handle, mask, exclusive, curState))
                return;

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

            // See CompileBufferBarriers's release-barrier comment above.
            ImageBarrier barrier;
            barrier.m_image     = target;
            barrier.m_srcUsage  = AttachmentUsage::Copy;
            barrier.m_srcAccess = AttachmentAccess::Write;
            barrier.m_dstUsage  = AttachmentUsage::Uninitialized;
            barrier.m_dstAccess = AttachmentAccess::Unknown;
            barrier.m_srcStage  = AttachmentStage::Copy;
            barrier.m_dstStage  = AttachmentStage::Any;
            barrier.m_srcQueue  = HardwareQueueClass::Copy;
            barrier.m_dstQueue  = exclusive ? ResolveHomeQueue(mask) : HardwareQueueClass::Copy;
            batch.m_imageReleaseBarriers.push_back(barrier);

            touchedEntities.push_back(handle);

            ctx.Remove<UploadPendingTag>(handle);
            ctx.Remove<PendingImageUpload>(handle);
        });

        if (batch.m_bufferUploads.empty() && batch.m_imageUploads.empty())
        {
            return;
        }

        // Allocate the batch's fence value from a main-thread-private counter.
        // The actual fence pending value is updated later on the upload thread
        // when ProcessBatch's CommandQueue::Signal runs, keeping m_uploadFence's
        // pending value single-writer.
        batch.m_fenceValue = ++m_batchFenceValue;

        // Producer-side: stamp PendingSync on every touched entity with this
        // batch's (m_uploadFence, m_fenceValue). The next cross-queue consumer
        // (RG first-touch) reads it, emits queue.Wait, removes the component,
        // and emits the acquire barrier.
        const PendingSync sync{ m_uploadFence.get(), batch.m_fenceValue };
        for (RHIHandle handle : touchedEntities)
        {
            ctx.AddOrReplace<PendingSync>(handle, sync);
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

        // Pre-copy barriers: transition every target into Copy/Write on the copy
        // queue. ConvertTo* auto-populates src* from the resource's tracked state:
        //  - Fresh resource:     {Uninitialized, Graphics-default, Any}
        //  - Re-upload pickup:   whatever the prior owner left (e.g. {VertexBuffer,
        //                        Graphics, VertexInput}) — fence wait was already
        //                        cleared by SubmitBatch's CPU-side skip-or-proceed
        //                        check, so prior GPU work is guaranteed complete.
        // dstQueue is overridden to Copy. Because the resource's tracked m_queue
        // is rarely Copy in practice, the backend's cross-queue acquire path runs
        // (DX12: COMMON → COPY_DEST regardless of srcUsage; Vulkan CONCURRENT:
        // pipeline barrier with no QFOT). dstStage pinned to Copy.
        for (const auto& upload : batch.m_bufferUploads)
        {
            BufferBarrier pre = ConvertToCopyWrite(*upload.m_targetBuffer);
            pre.m_dstQueue = HardwareQueueClass::Copy;
            pre.m_dstStage = AttachmentStage::Copy;
            cmdList->QueueBarrier(pre);
        }
        for (const auto& upload : batch.m_imageUploads)
        {
            ImageBarrier pre = ConvertToImageCopyWrite(*upload.m_targetImage);
            pre.m_dstQueue = HardwareQueueClass::Copy;
            pre.m_dstStage = AttachmentStage::Copy;
            cmdList->QueueBarrier(pre);
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
        for (const auto& upload : batch.m_imageUploads)
        {
            // Query per-subresource layouts from the Image. The layout
            // already contains byte-aligned row pitch and image size for DX12.
            ImageSubresourceLayout layouts[RHI::Limits::Image::MipCountMax];
            upload.m_targetImage->GetSubresourceLayouts(upload.m_range, layouts, nullptr);

            const auto& imageDesc = upload.m_targetImage->GetDescriptor();
            const uint32_t mipLevels = imageDesc.m_mipLevels;
            const uint8_t* srcData = static_cast<const uint8_t*>(upload.m_data);

            for (uint16_t arraySlice = upload.m_range.m_arraySliceMin; arraySlice <= upload.m_range.m_arraySliceMax; ++arraySlice)
            {
                for (uint16_t mipSlice = upload.m_range.m_mipSliceMin; mipSlice <= upload.m_range.m_mipSliceMax; ++mipSlice)
                {
                    const uint32_t subresourceIndex = GetImageSubresourceIndex(mipSlice, arraySlice, mipLevels);
                    const ImageSubresourceLayout& layout = layouts[subresourceIndex];

                    // GPU-side layout (already aligned by GenerateSubresourceLayouts for
                    // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT). Used for staging-buffer writes
                    // and the CopyBufferToImage descriptor.
                    const uint32_t dstRowPitch  = layout.m_bytesPerRow;
                    const uint32_t dstTotalSize = layout.m_bytesPerImage;
                    const uint32_t numRows      = layout.m_rowCount;

                    // CPU-side layout (unaligned). ImageAsset stores mips tightly packed;
                    // we must not read past the end of each source row.
                    RHI::ImageSubresourceLayout srcLayout =
                        RHI::GetImageSubresourceLayout(imageDesc, RHI::ImageSubresource(mipSlice, arraySlice));
                    const uint32_t srcRowPitch     = srcLayout.m_bytesPerRow;
                    const uint32_t srcBytesPerImage = srcLayout.m_bytesPerImage;

                    // DX12 CopyTextureRegion requires the source buffer offset to be
                    // aligned to TexturePlacement (512). Pad current offset up; previous
                    // buffer uploads leave the packet at arbitrary alignment.
                    uint32_t alignedOffset = AlignUp(packet->m_offset, RHI::Alignment::TexturePlacement);

                    if (alignedOffset + dstTotalSize > m_descriptor.m_stagingSizeInBytes)
                    {
                        SubmitFramePacket();
                        alignedOffset = 0;
                    }
                    packet->m_offset = alignedOffset;

                    const uint8_t* srcRow = srcData;
                    uint8_t* dstRow = packet->m_mappedPtr + packet->m_offset;
                    for (uint32_t row = 0; row < numRows; ++row)
                    {
                        memcpy(dstRow, srcRow, srcRowPitch);
                        srcRow += srcRowPitch;
                        dstRow += dstRowPitch;
                    }
                    srcData += srcBytesPerImage;

                    CopyBufferToImageDescriptor copyDesc;
                    copyDesc.m_sourceBuffer          = packet->m_stagingBuffer.get();
                    copyDesc.m_sourceOffset          = packet->m_offset;
                    copyDesc.m_sourceBytesPerRow     = dstRowPitch;
                    copyDesc.m_sourceBytesPerImage   = dstTotalSize;
                    copyDesc.m_sourceFormat          = upload.m_sourceFormat;
                    copyDesc.m_sourceSize            = layout.m_size;
                    copyDesc.m_destinationImage      = upload.m_targetImage;
                    copyDesc.m_destinationSubresource = ImageSubresource(mipSlice, arraySlice);
                    copyDesc.m_destinationOrigin     = upload.m_destinationOrigin;

                    cmdList->Submit(CopyItem{ copyDesc });

                    packet->m_offset += dstTotalSize;
                }
            }
        }

        // Release barriers: cross-queue handoff to the destination queue. The
        // backend sees srcQueue == myQueue (Copy) and emits the release half —
        // for DX12 this is target → COMMON. The acquire half is emitted later
        // by the RenderGraph executer when a pass first uses the resource
        // (paired with a fence wait on m_uploadFence).
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
