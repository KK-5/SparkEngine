#pragma once

#include <EASTL/type_traits.h>
#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <Object/ObjectName.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>

#include <Staging/ElementUpdateBuffer.h>
#include <Suballocation/ReservedBuffer.h>
#include <Suballocation/Suballocator.h>

namespace Spark::RHI
{
    class Device;
}

namespace Spark::Render
{
    //! An array in device memory whose elements keep their index for life: a reserved buffer
    //! backed page by page as elements are taken, bound once as a structured view. Each frame
    //! it is given every held element and writes the ones that differ from what the GPU holds,
    //! through updates its owner's pass applies. The other way to put an array on the GPU is a
    //! StagingBuffer rewritten whole each frame, for small ones that change every frame.
    //!
    //! A frame goes: elements taken, BeginUpdate, Set for every held element, EndUpdate.
    class DeviceArray
    {
    public:
        struct Descriptor
        {
            ObjectName     m_name;
            //! A whole number of uints: ElementUpdate.hlsl copies a uint at a time.
            uint32_t       m_elementSize = 0;
            uint32_t       m_capacity    = 0;
            //! The structured input on m_bindings the array is bound to.
            RHI::InputName m_inputName;
            RHI::RHIHandle m_bindings    = RHI::NullHandle;
        };

        bool Init(RHI::RHIContext& rhiCtx, RHI::Device& device, const Descriptor& descriptor);
        //! After the GPU is idle, and after every element is gone.
        void Shutdown(RHI::RHIContext& rhiCtx);

        //! An element; its index is its offset over the element size. Empty when every one is taken.
        Suballocation Allocate();

        //! Every frame, once the frame's elements are taken and before any Set: makes room for them.
        void BeginUpdate();

        //! A held element's value this frame.
        template<typename T>
        void Set(uint32_t index, const T& value)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "An element is compared and copied as bytes.");
            ASSERT(sizeof(T) == m_descriptor.m_elementSize, "[DeviceArray] {}: a {}-byte value set as a {}-byte element.",
                m_descriptor.m_name.GetCStr(), sizeof(T), m_descriptor.m_elementSize);
            SetBytes(index, &value);
        }

        //! Every frame, after the last Set: stages the elements that differ, for the pass to write.
        void EndUpdate();

        //! The buffer as the graph imports it, which its owner tags.
        RHI::RHIHandle GetResource() const { return m_resource; }
        //! This frame's writes, for the pass that applies them.
        const ElementUpdateBuffer& GetUpdates() const { return m_updates; }
        uint64_t GetByteCount() const { return uint64_t(m_descriptor.m_capacity) * m_descriptor.m_elementSize; }

    private:
        void SetBytes(uint32_t index, const void* bytes);

        Descriptor                              m_descriptor;
        Ptr<Suballocator>                       m_allocator;
        ReservedBuffer                          m_pool;
        eastl::vector<Suballocator::PageChange> m_pageChanges;
        RHI::RHIHandle                          m_resource = RHI::NullHandle;
        ElementUpdateBuffer                     m_updates;

        //! What the GPU holds in each element, where m_known says it is known.
        eastl::vector<uint8_t> m_elements;
        eastl::vector<uint8_t> m_known;
        //! This frame's elements that differ from what the GPU holds.
        eastl::vector<uint32_t> m_changed;
    };
}
