#pragma once

#include <string.h>

#include <EASTL/type_traits.h>

#include <Base.h>
#include <Log/ILogSystem.h>
#include <Object/ObjectName.h>
#include <RHI/Context/RHIHandle.h>

#include "StagingBuffer.h"

namespace Spark::RHI
{
    class Device;
}

namespace Spark::Render
{
    //! The frame's new contents of some elements of an array: their indices, then the contents,
    //! packed in a StagingBuffer of its own that a pass of its system reads with
    //! ElementUpdate.hlsl, writing each into its element. An index is given once a frame.
    class ElementUpdateBuffer
    {
    public:
        struct Descriptor
        {
            ObjectName m_name;
            //! A whole number of uints: the shader copies a uint at a time.
            uint32_t   m_elementSize = 0;
        };

        bool Init(RHI::Device& device, const Descriptor& descriptor);
        void Shutdown();

        //! Every frame, writing or not, with how many elements it may update.
        void Begin(uint32_t updateCount);

        //! Element `index`'s new contents, of m_elementSize bytes. Past the Begin count, dropped.
        template<typename T>
        void Set(uint32_t index, const T& value)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "An element is copied as bytes.");
            ASSERT(sizeof(T) == m_elementSize, "[ElementUpdateBuffer] A {}-byte value set as a {}-byte element.",
                sizeof(T), m_elementSize);
            if (uint8_t* element = Append(index))
            {
                memcpy(element, &value, sizeof(T));
            }
        }

        //! The elements set this frame.
        uint32_t GetUpdateCount() const { return m_updateCount; }
        uint32_t GetElementSize() const { return m_elementSize; }
        uint64_t GetIndicesOffset() const { return m_indicesOffset; }
        uint64_t GetElementsOffset() const { return m_elementsOffset; }
        //! From the start of the buffer to the end of the contents.
        uint64_t GetByteCount() const { return m_elementsOffset + uint64_t(m_updateCount) * m_elementSize; }

        //! This frame's buffer, for the pass to import.
        RHI::RHIHandle GetResource() const { return m_staging.GetResource(); }

    private:
        //! Records `index` and returns where its contents go; null past the Begin count.
        uint8_t* Append(uint32_t index);

        StagingBuffer m_staging;
        uint32_t      m_elementSize = 0;

        uint32_t  m_capacity       = 0;
        uint32_t  m_updateCount    = 0;
        uint64_t  m_indicesOffset  = 0;
        uint64_t  m_elementsOffset = 0;
        uint32_t* m_indices        = nullptr;
        uint8_t*  m_elements       = nullptr;
    };
}
