#pragma once

#include <EASTL/vector.h>

#include <Base.h>
#include <Object/ObjectName.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/Buffer/BufferBindFlags.h>

namespace Spark::RHI
{
    class Buffer;
    class BufferPool;
    class Device;
}

namespace Spark::Render
{
    //! Host memory a system writes the frame's data into for a pass of its own to read: one
    //! buffer per frame in flight, each an imported resource of its own, kept mapped. The
    //! system turns to this frame's at Begin, saying how much it writes, and carves that up.
    //! A buffer is used again frameCountMax frames on, by when the frame fences have waited
    //! out the GPU. Not thread safe: only its system writes it, in its update.
    class StagingBuffer
    {
    public:
        struct Descriptor
        {
            ObjectName           m_name;
            //! CopyRead for a copy pass, ShaderRead as well for one that reads it as a view.
            RHI::BufferBindFlags m_bindFlags       = RHI::BufferBindFlags::CopyRead;
            uint64_t             m_initialCapacity = 64 * 1024;
        };

        struct Allocation
        {
            uint64_t m_offset = 0;
            void*    m_data   = nullptr;
        };

        StagingBuffer() = default;
        StagingBuffer(const StagingBuffer&)            = delete;
        StagingBuffer& operator=(const StagingBuffer&) = delete;
        ~StagingBuffer();

        bool Init(RHI::Device& device, const Descriptor& descriptor);
        void Shutdown();

        //! Turns to this frame's buffer, to write `byteCount` bytes into before the graph
        //! builds. Every frame, writing or not. A buffer short of it is replaced by a larger
        //! one now, while nothing is in it.
        void Begin(uint64_t byteCount);

        //! `byteCount` bytes of what Begin was told, from the first multiple of `alignment`
        //! on. Past it, nothing.
        Allocation Allocate(uint64_t byteCount, uint64_t alignment = 1);

        //! This frame's buffer, for the pass to import.
        RHI::RHIHandle GetResource() const;
        //! This frame's buffer, for a view a shader reads it by.
        RHI::Buffer* GetBuffer() const;

        uint64_t GetUsedBytes() const { return m_usedBytes; }

    private:
        struct Slot
        {
            Ptr<RHI::Buffer> m_buffer;
            uint8_t*         m_data     = nullptr;
            RHI::RHIHandle   m_resource = RHI::NullHandle;
        };

        bool CreateSlot(Slot& slot, uint32_t index, uint64_t capacity);
        void DestroySlot(Slot& slot);

        RHI::Device*         m_device = nullptr;
        Descriptor           m_descriptor;
        Ptr<RHI::BufferPool> m_pool;
        eastl::vector<Slot>  m_slots;

        uint32_t m_current       = 0;
        uint64_t m_reservedBytes = 0;
        uint64_t m_usedBytes     = 0;
    };
}
