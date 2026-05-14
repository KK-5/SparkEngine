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
            const void*      m_data                = nullptr;
            size_t           m_dataSize            = 0;
            Image*           m_targetImage         = nullptr;
            ImageSubresource m_subresource {};
            Origin           m_destinationOrigin {};
            Size             m_size {};
            Format           m_sourceFormat        = Format::Unknown;
            uint32_t         m_sourceBytesPerRow   = 0;
            uint32_t         m_sourceBytesPerImage = 0;
        };

        struct Batch
        {
            uint64_t                     m_fenceValue;
            eastl::vector<BufferUpload>  m_bufferUploads;
            eastl::vector<ImageUpload>   m_imageUploads;
        };

        // CPU-blocking sync flush for init-time paths.
        void FlushUploadPackets();

        void PollCompletions(RHIContext& ctx);
        void SubmitBatch(RHIContext& ctx);

        void UploadThreadMain();
        void ProcessBatch(Batch& batch);

        Descriptor               m_descriptor;
        Ptr<CommandQueue>        m_copyQueue;
        Ptr<BufferPool>          m_stagingPool;
        eastl::vector<FramePacket> m_packets;
        uint32_t                 m_currentPacketIndex = 0;

        // External contract fence — signalled exactly once per batch (final value).
        // Consumers (UploadSubmitted, RG cross-queue wait, FlushAndWait) check this.
        Ptr<Fence>               m_uploadFence;

        std::mutex               m_mutex;
        std::condition_variable  m_cv;
        eastl::deque<Batch>      m_pendingBatches;
        std::thread              m_uploadThread;
        eastl::atomic<bool>      m_running {false};
    };
}
