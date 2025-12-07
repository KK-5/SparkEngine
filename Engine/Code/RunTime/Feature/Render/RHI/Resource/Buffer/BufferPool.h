#pragma once

#include <EASTL/atomic.h>

#include <Resource/ResourcePool.h>
#include "BufferPoolDescriptor.h"
#include "Buffer.h"

namespace Spark::Render::RHI
{
    class Fence;

    struct BufferInitRequest
    {
        BufferInitRequest() = default;

        BufferInitRequest(Buffer& buffer, const BufferDescriptor& descriptor, const void* initialData = nullptr)
            : m_buffer{ &buffer }
            , m_descriptor{ descriptor }
            , m_initialData{ initialData }
        {}

        /// The buffer to initialize. The buffer must be in an uninitialized state.
        Buffer* m_buffer = nullptr;

        /// The descriptor used to initialize the buffer.
        BufferDescriptor m_descriptor;

        /// [Optional] Initial data used to initialize the buffer.
        const void* m_initialData = nullptr;
    };

    template <typename BufferClass>
    struct BufferMapRequestTemplate
    {
        BufferMapRequestTemplate() = default;

        BufferMapRequestTemplate(BufferClass& buffer, size_t byteOffset, size_t byteCount)
            : m_buffer{&buffer}
            , m_byteOffset{byteOffset}
            , m_byteCount{byteCount}
            {}

        /// The buffer instance to map for CPU access.
        BufferClass* m_buffer = nullptr;

        /// The number of bytes offset from the base of the buffer to map for access.
        size_t m_byteOffset = 0;

        /// The number of bytes beginning from the offset to map for access.
        size_t m_byteCount = 0;
    };

    struct BufferMapResponse
    {
        void* m_data = nullptr;
    };

    template <typename BufferClass, typename FenceClass>
    struct BufferStreamRequestTemplate
    {
        /// A fence to signal on completion of the upload operation.
        FenceClass* m_fenceToSignal = nullptr;

        /// The buffer instance to stream up to.
        BufferClass* m_buffer = nullptr;

        /// The number of bytes offset from the base of the buffer to start the upload.
        size_t m_byteOffset = 0;

        /// The number of bytes to upload beginning from m_byteOffset.
        size_t m_byteCount = 0;

        /// A pointer to the source data to upload. The source data must remain valid
        /// for the duration of the upload operation (i.e. until m_callbackFunction
        /// is invoked).
        const void* m_sourceData = nullptr;
    };

    using BufferMapRequest = BufferMapRequestTemplate<Buffer>;
    using BufferStreamRequest = BufferStreamRequestTemplate<Buffer, Fence>;

    class BufferPool : public ResourcePool
    {
    public:
        virtual ~BufferPool() = default;

        ResultCode Init(Device& device, const BufferPoolDescriptor& descriptor);

        ResultCode InitBuffer(const BufferInitRequest& request);

        ResultCode OrphanBuffer(Buffer& buffer);

        ResultCode MapBuffer(const BufferMapRequest& request, BufferMapResponse& response);

        void UnmapBuffer(Buffer& buffer);

        ResultCode StreamBuffer(const BufferStreamRequest& request);

        const BufferPoolDescriptor& GetDescriptor() const override final;

    protected:
        BufferPool() = default;

        // FrameEventBus
        void OnFrameBegin() override;

        uint32_t GetMapRefCount() const;

    private:
        using ResourcePool::InitResource;

        void ValidateBufferMap(Buffer& buffer, bool isDataValid);
        bool ValidateBufferUnmap(Buffer& buffer);
        bool ValidatePoolDescriptor(const BufferPoolDescriptor& descriptor) const;
        bool ValidateInitRequest(const BufferInitRequest& initRequest) const;
        bool ValidateIsHostHeap() const;
        bool ValidateMapRequest(const BufferMapRequest& request) const;

        //////////////////////////////////////////////////////////////////////////
        // Backend API

        /// Called when the pool is being initialized.
        virtual ResultCode InitInternal(Device& device, const RHI::BufferPoolDescriptor& descriptor) = 0;

        /// Called when a buffer is being initialized onto the pool.
        virtual ResultCode InitBufferInternal(Buffer& buffer, const BufferDescriptor& descriptor) = 0;

        /// Called when the buffer is being orphaned.
        virtual ResultCode OrphanBufferInternal(Buffer& buffer) = 0;

        /// Called when a buffer is being mapped.
        virtual ResultCode MapBufferInternal(const BufferMapRequest& request, BufferMapResponse& response) = 0;

        /// Called when a buffer is being unmapped.
        virtual void UnmapBufferInternal(Buffer& buffer) = 0;

        /// Called when a buffer is being streamed asynchronously.
        virtual ResultCode StreamBufferInternal(const BufferStreamRequest& request);

        //Called in order to do a simple mem copy allowing Null rhi to opt out
        virtual void BufferCopy(void* destination, const void* source, size_t num);

        //////////////////////////////////////////////////////////////////////////
        
        BufferPoolDescriptor m_descriptor;
        eastl::atomic<uint32_t> m_mapRefCount = {0};
    };
    
}