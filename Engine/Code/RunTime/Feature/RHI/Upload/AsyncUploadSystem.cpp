#include "AsyncUploadSystem.h"

#include <Service/Service.h>
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
        Service<AsyncUploadSystem>::Register(this);

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
        {
            m_uploadFence = factory->CreateFence();
            if (m_uploadFence->Init(*device, FenceState::Reset) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init upload fence.");
                return;
            }
        }

        // Packet fence — internal staging rotation tracker, upload-thread private.
        {
            m_packetFence = factory->CreateFence();
            if (m_packetFence->Init(*device, FenceState::Reset) != ResultCode::Success)
            {
                LOG_ERROR("[AsyncUploadSystem] Failed to init packet fence.");
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

        FlushAndWait();

        Service<AsyncUploadSystem>::Unregister(this);

        for (auto& packet : m_packets)
        {
            m_stagingPool->UnmapBuffer(*packet.m_stagingBuffer);
            packet.m_stagingBuffer.reset();
        }
        m_packets.clear();

        m_packetFence.reset();
        m_uploadFence.reset();
        m_stagingPool.reset();
        m_copyQueue.reset();
    }

    void AsyncUploadSystem::OnFrameBegin()
    {
        auto& ctx = *RHIExecuteContext::Current();

        PollCompletions(ctx);
        SubmitBatch(ctx);
    }

    void AsyncUploadSystem::FlushAndWait()
    {
        const uint64_t pending = m_uploadFence->GetPendingValue();
        if (pending > m_uploadFence->GetCompletedValue())
        {
            m_uploadFence->WaitOnCpu();
        }
    }

    void AsyncUploadSystem::PollCompletions(RHIContext& ctx)
    {
        const uint64_t completed = m_uploadFence->GetCompletedValue();

        auto view = ctx.GetView<UploadSubmitted>();
        eastl::vector<RHIHandle> toRemove;

        view.each([&](RHIHandle handle, const UploadSubmitted& submitted)
        {
            if (submitted.m_fenceValue <= completed)
            {
                toRemove.push_back(handle);
            }
        });

        for (RHIHandle handle : toRemove)
        {
            ctx.Remove<UploadSubmitted>(handle);
        }
    }

    void AsyncUploadSystem::SubmitBatch(RHIContext& ctx)
    {
        auto bufferView = ctx.GetView<UploadPendingTag, PendingBufferUpload>();
        auto imageView  = ctx.GetView<UploadPendingTag, PendingImageUpload>();

        Batch batch;
        batch.m_bufferUploads.reserve(bufferView.size_hint());
        batch.m_imageUploads.reserve(imageView.size_hint());

        eastl::vector<RHIHandle> submittedHandles;
        submittedHandles.reserve(bufferView.size_hint() + imageView.size_hint());

        bufferView.each([&](RHIHandle handle, const PendingBufferUpload& pending)
        {
            auto* owning = ctx.TryGet<Components::Buffer>(handle);
            if (!owning || !owning->m_buffer)
            {
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
                              "but has no materialized Buffer. Make sure BufferDescriptor "
                              "is declared and RHIResourceSystem runs before this system.",
                              static_cast<uint32_t>(handle));
                }
                // Clear pending state so the caller can release m_data and the entity
                // doesn't get retried every frame forever.
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingBufferUpload>(handle);
                return;
            }

            BufferUpload upload;
            upload.m_data              = pending.m_data;
            upload.m_dataSize          = pending.m_dataSize;
            upload.m_targetBuffer      = owning->m_buffer.get();
            upload.m_destinationOffset = pending.m_destinationOffset;
            batch.m_bufferUploads.push_back(upload);

            submittedHandles.push_back(handle);
            ctx.Remove<UploadPendingTag>(handle);
            ctx.Remove<PendingBufferUpload>(handle);
        });

        imageView.each([&](RHIHandle handle, const PendingImageUpload& pending)
        {
            auto* owning = ctx.TryGet<Components::Image>(handle);
            if (!owning || !owning->m_image)
            {
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
                              "but has no materialized Image. Make sure ImageDescriptor "
                              "is declared and RHIResourceSystem runs before this system.",
                              static_cast<uint32_t>(handle));
                }
                ctx.Remove<UploadPendingTag>(handle);
                ctx.Remove<PendingImageUpload>(handle);
                return;
            }

            ImageUpload upload;
            upload.m_data                = pending.m_data;
            upload.m_dataSize            = pending.m_dataSize;
            upload.m_targetImage         = owning->m_image.get();
            upload.m_subresource         = pending.m_subresource;
            upload.m_destinationOrigin   = pending.m_destinationOrigin;
            upload.m_size                = pending.m_size;
            upload.m_sourceFormat        = pending.m_sourceFormat;
            upload.m_sourceBytesPerRow   = pending.m_sourceBytesPerRow;
            upload.m_sourceBytesPerImage = pending.m_sourceBytesPerImage;
            batch.m_imageUploads.push_back(upload);

            submittedHandles.push_back(handle);
            ctx.Remove<UploadPendingTag>(handle);
            ctx.Remove<PendingImageUpload>(handle);
        });

        if (batch.m_bufferUploads.empty() && batch.m_imageUploads.empty())
        {
            return;
        }

        batch.m_fenceValue = m_uploadFence->Increment();

        ctx.Add<UploadSubmitted>(submittedHandles.begin(), submittedHandles.end(), UploadSubmitted{ batch.m_fenceValue });

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
        auto* rhi = Service<RHIInterface>::Get();
        auto* factory = rhi->GetRHIFactory();
        auto* device = rhi->GetDevice();

        CommandList* cmdList = factory->CreateCommandList(*device, HardwareQueueClass::Copy);
        cmdList->Open();

        auto* packet = &m_packets[m_currentPacketIndex];

        // Mid-batch flush: staging packet is full, retire it and rotate to the next.
        // Uses m_packetFence (upload-thread private) so m_uploadFence stays clean —
        // exactly one Signal per batch lands on the external contract fence.
        auto SubmitFramePacket = [&]()
        {
            cmdList->Close();
            m_copyQueue->ExecuteCommands({ &cmdList, 1 });

            packet->m_fenceValue = m_packetFence->Increment();
            m_copyQueue->Signal(*m_packetFence);

            m_currentPacketIndex = (m_currentPacketIndex + 1) % m_packets.size();
            packet = &m_packets[m_currentPacketIndex];

            // Wait until the GPU is done consuming this packet's previous use.
            if (packet->m_fenceValue > m_packetFence->GetCompletedValue())
            {
                m_packetFence->WaitOnCpu();
            }
            packet->m_offset = 0;

            cmdList = factory->CreateCommandList(*device, HardwareQueueClass::Copy);
            cmdList->Open();
        };

        // Process buffer uploads
        for (const auto& upload : batch.m_bufferUploads)
        {
            if (packet->m_offset + upload.m_dataSize > m_descriptor.m_stagingSizeInBytes)
            {
                SubmitFramePacket();
            }

            const auto* src = static_cast<const uint8_t*>(upload.m_data);
            memcpy(packet->m_mappedPtr + packet->m_offset, src, upload.m_dataSize);

            CopyBufferDescriptor copyDesc;
            copyDesc.m_sourceBuffer      = packet->m_stagingBuffer.get();
            copyDesc.m_sourceOffset      = packet->m_offset;
            copyDesc.m_destinationBuffer = upload.m_targetBuffer;
            copyDesc.m_destinationOffset = static_cast<uint32_t>(upload.m_destinationOffset);
            copyDesc.m_size              = static_cast<uint32_t>(upload.m_dataSize);

            cmdList->Submit(CopyItem{ copyDesc });

            packet->m_offset += static_cast<uint32_t>(upload.m_dataSize);
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

        // Batch end: single Signal on m_uploadFence carries the external contract.
        // Also stamp m_packetFence on the current packet so future rotations can
        // tell the GPU is done with this packet's data.
        cmdList->Close();
        m_copyQueue->ExecuteCommands({ &cmdList, 1 });
        m_copyQueue->Signal(*m_uploadFence, batch.m_fenceValue);

        packet->m_fenceValue = m_packetFence->Increment();
        m_copyQueue->Signal(*m_packetFence);
    }

}
