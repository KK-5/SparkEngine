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
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandRecorder.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Resource/Buffer/BufferPool.h>

namespace Spark::RHI
{
    class AsyncUploadSystem final : public ISystem
                                  , public FrameEventBus::Handler
    {

    public:
        struct Descriptor
        {
            size_t m_stagingSizeInBytes = 120 * 1024 * 1024;
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

        // Source data + resolved target, packed for the upload thread.
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

        // Cross-system handoff fence wait, recorded by SubmitBatch when a
        // target carries a PendingSync from a different queue's previous
        // submission. Upload thread emits queue.Wait before the pre-copy
        // barrier so the prior owner's GPU work is guaranteed complete.
        struct FenceWait
        {
            Fence*   m_fence = nullptr;
            uint64_t m_value = 0;
        };

        struct Batch
        {
            uint64_t                     m_fenceValue;
            eastl::vector<BufferUpload>  m_bufferUploads;
            eastl::vector<ImageUpload>   m_imageUploads;

            // Release barriers constructed on the main thread in SubmitBatch.
            // CONCURRENT targets: intra-Copy Copy/Write → Uninitialized (lands at COMMON).
            // EXCLUSIVE targets:  cross-queue Copy → homeQueue, srcUsage=Copy/Write → Uninit.
            // The upload thread flushes them after copies.
            // Indices align with m_bufferUploads / m_imageUploads respectively.
            eastl::vector<BufferBarrier> m_bufferReleaseBarriers;
            eastl::vector<ImageBarrier>  m_imageReleaseBarriers;
        };

        // CPU-blocking sync flush for init-time paths.
        void FlushUploadPackets();

        void SubmitBatch(RHIContext& ctx);

        void UploadThreadMain();
        void ProcessBatch(Batch& batch);

        Descriptor               m_descriptor;
        Ptr<CommandQueue>        m_copyQueue;
        Ptr<BufferPool>          m_stagingPool;
        eastl::vector<FramePacket> m_packets;
        uint32_t                 m_currentPacketIndex = 0;

        // External contract fence — signalled exactly once per batch (final value).
        // PendingSync carries a raw pointer to this fence so the RG barrier
        // compiler can record the paired graphics-queue fence wait + acquire barrier.
        // The upload thread writes fence values via CommandQueue::Signal;
        // the main thread allocates batch fence values through m_batchFenceValue
        // and never touches the fence directly.
        Ptr<Fence>               m_uploadFence;
        uint64_t                 m_batchFenceValue = 0;

        std::mutex               m_mutex;
        std::condition_variable  m_cv;
        eastl::deque<Batch>      m_pendingBatches;
        std::thread              m_uploadThread;
        eastl::atomic<bool>      m_running {false};
    };
}
