#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>

#include <EASTL/atomic.h>
#include <EASTL/deque.h>
#include <EASTL/vector.h>

#include <ECS/ISystem.h>

#include <RHI/Base.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Component/Component.h>
#include <RHI/Component/EventComponent.h>
#include <RHI/Context/RHIEventContext.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandRecorder.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Resource/Buffer/BufferPool.h>

namespace Spark::RHI
{
    class CommandQueueContext;
    class Device;

    class AsyncUploadSystem final : public ISystem
                                  , public FrameEventBus::Handler
    {

    public:
        struct Descriptor
        {
            size_t m_stagingSizeInBytes = 16 * 1024 * 1024;
        };

        AsyncUploadSystem() = default;
        explicit AsyncUploadSystem(const Descriptor& desc) : m_descriptor(desc) {}

        // ISystem
        eastl::vector<HashString> Request() const override { return {"RHI"_hs}; }
        HashString GetName() const override { return "AsyncUploadSystem"_hs; }

        // FrameEventBus
        void OnFrameBegin() override;

    protected:
        void InitInternal()     override;
        void ShutdownInternal() override;

    private:
        struct FramePacket
        {
            Ptr<Buffer>           m_stagingBuffer;
            uint8_t*              m_mappedPtr       = nullptr;
            uint32_t              m_offset          = 0;
            uint64_t              m_fenceValue      = 0;
            Ptr<Fence>            m_fence;
            Ptr<CommandRecorder>  m_commandRecorder;
        };

        // Source data + resolved target, packed for the upload thread. The request, which
        // outlives the batch, is what keeps the target alive (UploadBatch).
        struct BufferUpload
        {
            const void* m_data              = nullptr;
            size_t      m_dataSize          = 0;
            Buffer*     m_targetBuffer      = nullptr;
            uint64_t    m_destinationOffset = 0;
        };

        struct ImageUpload
        {
            const void*           m_data              = nullptr;
            size_t                m_dataSize          = 0;
            Image*                m_targetImage       = nullptr;
            ImageSubresourceRange m_range {};
            Origin                m_destinationOrigin {};
            Format                m_sourceFormat      = Format::Unknown;
        };

        struct Batch
        {
            uint64_t                     m_fenceValue;
            eastl::vector<BufferUpload>  m_bufferUploads;
            eastl::vector<ImageUpload>   m_imageUploads;

            // What the upload thread records around the copies, made on the main thread in
            // SubmitBatch from the requests alone: the upload thread reads no resource state.
            // An image is turned into a copy destination first; what one queue owns is
            // released to it afterwards. A buffer several queues share takes neither.
            eastl::vector<ImageBarrier>        m_imagePreBarriers;
            eastl::vector<BufferQueueTransfer> m_bufferReleases;
            eastl::vector<ImageQueueTransfer>  m_imageReleases;
        };

        // CPU-blocking sync flush for init-time paths.
        void FlushUploadPackets();

        //! Moves the requests whose batch the GPU has finished on: to awaiting their acquire,
        //! which the Scope that declares a read of the target records, or to their end when
        //! they need none.
        void FinishUploads(RHIContext& ctx, RHIEventContext& events);

        void SubmitBatch(RHIContext& ctx, RHIEventContext& events);

        void UploadThreadMain();
        void ProcessBatch(Batch& batch);

        Descriptor               m_descriptor;
        Ptr<CommandQueue>        m_copyQueue;
        Ptr<BufferPool>          m_stagingPool;
        eastl::vector<FramePacket> m_packets;
        uint32_t                 m_currentPacketIndex = 0;

        // Signalled exactly once per batch (final value); a request's copy is over once it
        // reaches the value its UploadBatch holds.
        // The upload thread writes fence values via CommandQueue::Signal;
        // the main thread allocates batch fence values through m_batchFenceValue
        // and only reads the completed value.
        Ptr<Fence>               m_uploadFence;
        uint64_t                 m_batchFenceValue = 0;

        std::mutex               m_mutex;
        std::condition_variable  m_cv;
        eastl::deque<Batch>      m_pendingBatches;
        std::thread              m_uploadThread;
        eastl::atomic<bool>      m_running {false};
    };
}
