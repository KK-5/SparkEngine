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
    }

    void AsyncUploadSystem::ShutdownInternal()
    {
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
            packet.m_stagingBuffer.reset();
        }
        m_packets.clear();

        m_uploadFence.reset();
        m_stagingPool.reset();
        m_copyQueue.reset();
    }

    void AsyncUploadSystem::OnFrameBegin()
    {
        auto& ctx = *RHIExecuteContext::Current();

        // Note: there is no PollCompletions step here. Submitted upload entities
        // carry BufferUploadSubmitted / ImageUploadSubmitted with the cross-queue
        // acquire barrier. The RenderGraph executer consumes those components
        // (emits the acquire on graphics queue + removes the component) when the
        // resource is first used and the fence is ready. This avoids the race
        // where a CPU-side poll could clear the component before the executer
        // had a chance to emit the acquire barrier.
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

        // Touched entities, collected so we can stamp PendingSync after the
        // batch's fence value is allocated.
        eastl::vector<RHIHandle> touchedEntities;
        touchedEntities.reserve(bufferView.size_hint() + imageView.size_hint());

        // Inline resolution of EXCLUSIVE vs CONCURRENT sharing mode and the
        // home-queue extraction from m_sharedQueueMask. Single bit (and not Copy
        // alone) → EXCLUSIVE; multi bit → CONCURRENT. No helper functions per
        // codebase convention — just power-of-two and direct mask compare.
        auto IsExclusive = [](HardwareQueueClassMask mask) -> bool
        {
            const uint32_t m = static_cast<uint32_t>(mask);
            return m != 0 && (m & (m - 1)) == 0;
        };
        auto ResolveHomeQueue = [](HardwareQueueClassMask mask) -> HardwareQueueClass
        {
            if (mask == HardwareQueueClassMask::Compute) return HardwareQueueClass::Compute;
            if (mask == HardwareQueueClassMask::Copy)    return HardwareQueueClass::Copy;
            return HardwareQueueClass::Graphics;
        };

        // Consumer protocol: if target has a PendingSync from a non-Copy queue,
        // emit a queue.Wait before pre-copy and remove the component. Same-queue
        // PendingSync (rare — e.g. a previous Copy queue submission) is left
        // alone because intra-queue submission ordering provides happens-before.
        auto AbsorbPendingSync = [&](RHIHandle handle, const ResourceState& state)
        {
            if (state.m_queue == HardwareQueueClass::Copy)
            {
                return;
            }
            if (auto* sync = ctx.TryGet<PendingSync>(handle))
            {
                batch.m_preFenceWaits.push_back({ sync->m_fence, sync->m_fenceValue });
                ctx.Remove<PendingSync>(handle);
            }
        };

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

            // Validation: m_sharedQueueMask must be non-empty and, for EXCLUSIVE,
            // the home queue must not be Copy (Copy-only rendering resource is
            // a contradiction — nothing renders, just uploads forever).
            if (mask == HardwareQueueClassMask::None)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} target buffer has empty "
                          "m_sharedQueueMask; declare at least one consumer queue.",
                          static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return;
            }
            if (exclusive && ResolveHomeQueue(mask) == HardwareQueueClass::Copy)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} declares EXCLUSIVE Copy-only "
                          "m_sharedQueueMask, which has no consumer. Set at least one "
                          "non-Copy queue.", static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return;
            }
            // EXCLUSIVE re-upload would require the previous owner to emit a
            // QFOT release pair back to Copy, which we don't support — see
            // TODO_CrossSystemResourceSync.md. Caller must use CONCURRENT.
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
                return;
            }

            // Consumer-side: absorb any pending sync debt from a previous owner
            // (only meaningful for re-upload of CONCURRENT resources).
            AbsorbPendingSync(handle, curState);

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

            if (mask == HardwareQueueClassMask::None)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} target image has empty "
                          "m_sharedQueueMask; declare at least one consumer queue.",
                          static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return;
            }
            if (exclusive && ResolveHomeQueue(mask) == HardwareQueueClass::Copy)
            {
                LOG_ERROR("[AsyncUploadSystem] Entity {} declares EXCLUSIVE Copy-only "
                          "m_sharedQueueMask, which has no consumer. Set at least one "
                          "non-Copy queue.", static_cast<uint32_t>(handle));
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return;
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
                return;
            }

            AbsorbPendingSync(handle, curState);

            ImageUpload upload;
            upload.m_data                = pending.m_data;
            upload.m_dataSize            = pending.m_dataSize;
            upload.m_targetImage         = target;
            upload.m_subresource         = pending.m_subresource;
            upload.m_destinationOrigin   = pending.m_destinationOrigin;
            upload.m_size                = pending.m_size;
            upload.m_sourceFormat        = pending.m_sourceFormat;
            upload.m_sourceBytesPerRow   = pending.m_sourceBytesPerRow;
            upload.m_sourceBytesPerImage = pending.m_sourceBytesPerImage;
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
        packet->m_commandRecorder->Reset();
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

            // Wait until the GPU is done consuming this packet's previous use.
            if (packet->m_fenceValue > packet->m_fence->GetCompletedValue())
            {
                packet->m_fence->WaitOnCpu();
            }
            packet->m_offset = 0;

            packet->m_commandRecorder->Reset();
            cmdList = packet->m_commandRecorder->GetCommandList();
        };

        // Cross-system fence waits: each entry was recorded on the main thread
        // by SubmitBatch when a target carried a PendingSync from a non-Copy
        // queue's previous submission. Emit queue.Wait on the copy queue so the
        // prior owner's GPU work is guaranteed complete before pre-copy starts.
        for (const auto& w : batch.m_preFenceWaits)
        {
            m_copyQueue->Wait(*w.m_fence, w.m_value);
        }

        // Pre-copy barriers: transition every target into Copy/Write on the copy
        // queue. ConvertTo* helpers source srcUsage from the resource's tracked
        // state (Uninitialized after creation, or whatever the previous owner
        // left it in). We override the queue fields to make the barrier
        // semantically an intra-copy-queue transition — for both EXCLUSIVE
        // (implicit Vulkan first-acquire is allowed) and CONCURRENT (no QFOT
        // ever needed) since the prior cross-queue handoff is the fence wait
        // emitted just above, not a barrier.
        for (const auto& upload : batch.m_bufferUploads)
        {
            BufferBarrier pre = ConvertToCopyWrite(*upload.m_targetBuffer);
            pre.m_srcQueue = HardwareQueueClass::Copy;
            pre.m_dstQueue = HardwareQueueClass::Copy;
            pre.m_dstStage = AttachmentStage::Copy;
            cmdList->QueueBarrier(pre);
        }
        for (const auto& upload : batch.m_imageUploads)
        {
            ImageBarrier pre = ConvertToImageCopyWrite(*upload.m_targetImage);
            pre.m_srcQueue = HardwareQueueClass::Copy;
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
            const uint32_t srcRowPitch  = upload.m_sourceBytesPerRow;
            const uint32_t dstRowPitch  = AlignUp(srcRowPitch, RHI::Alignment::TexturePitch);
            const uint32_t numRows      = upload.m_size.m_height;
            const uint32_t dstTotalSize = dstRowPitch * numRows;

            // DX12 CopyTextureRegion requires the source buffer offset to be aligned
            // to TexturePlacement (512). Pad current offset up; previous buffer uploads
            // leave the packet at arbitrary alignment.
            uint32_t alignedOffset = AlignUp(packet->m_offset, RHI::Alignment::TexturePlacement);

            if (alignedOffset + dstTotalSize > m_descriptor.m_stagingSizeInBytes)
            {
                SubmitFramePacket();
                // Fresh packet starts at offset 0, which already satisfies TexturePlacement.
                alignedOffset = 0;
            }
            packet->m_offset = alignedOffset;

            const auto* srcRow = static_cast<const uint8_t*>(upload.m_data);
            uint8_t* dstRow = packet->m_mappedPtr + packet->m_offset;
            for (uint32_t row = 0; row < numRows; ++row)
            {
                memcpy(dstRow, srcRow, srcRowPitch);
                srcRow += srcRowPitch;
                dstRow += dstRowPitch;
            }

            CopyBufferToImageDescriptor copyDesc;
            copyDesc.m_sourceBuffer        = packet->m_stagingBuffer.get();
            copyDesc.m_sourceOffset        = packet->m_offset;
            copyDesc.m_sourceBytesPerRow   = dstRowPitch;
            copyDesc.m_sourceBytesPerImage = dstTotalSize;
            copyDesc.m_sourceFormat        = upload.m_sourceFormat;
            copyDesc.m_sourceSize          = upload.m_size;
            copyDesc.m_destinationImage    = upload.m_targetImage;
            copyDesc.m_destinationSubresource = upload.m_subresource;
            copyDesc.m_destinationOrigin   = upload.m_destinationOrigin;

            cmdList->Submit(CopyItem{ copyDesc });

            packet->m_offset += dstTotalSize;
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
        m_copyQueue->Signal(*m_uploadFence, batch.m_fenceValue);

        packet->m_fenceValue = packet->m_fence->Increment();
        m_copyQueue->Signal(*packet->m_fence);
    }

}
