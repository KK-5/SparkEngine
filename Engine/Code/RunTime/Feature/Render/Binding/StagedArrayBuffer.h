#pragma once

#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Object/ObjectName.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Shader/ShaderBindingsUtils.h>


namespace Spark::Render
{
    //! One shader-visible array of Element: a host per-frame StructuredBuffer plus the
    //! CPU mirror it is uploaded from.
    //!
    //! Deliberately has no Update(). It offers only bind / write / upload — WHICH entry
    //! an element lands in is the caller's policy, and that is the one thing its users
    //! do not share: g_Lights packs densely in iteration order, g_ShadowViews scatters by
    //! atlas row, GlobalBuffer addresses by a stable slot it allocates itself.
    //!
    //! The mirror is the one complete image, which is what lets every frame copy it
    //! whole: a partial copy would leave each in-flight copy missing the changes made
    //! while it was not the write target, and a stopped object would oscillate between
    //! its old and new record with a period of frameCountMax. It also sits in plain
    //! cacheable RAM, which is the write mode encoding wants — upload heaps are
    //! write-combined and read-modify-write scattered stores into them are slow.
    //!
    //! The owner creates the ShaderBindings entity (shader reflection, space id, its own
    //! tags) and hands it in — this only binds the SRV onto it each frame.
    template<typename Element>
    class StagedArrayBuffer
    {
    public:
        struct Descriptor
        {
            uint32_t       m_capacity       = 0;
            ObjectName     m_resourceName;
            RHI::InputName m_inputName;
            RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;
        };

        //! The buffer is requested, not created: RHIResourceSystem materializes the N
        //! copies at the next OnFrameBegin. Do NOT "optimize" this into a synchronous
        //! create — BindFrame returning false for a frame is the contract, not a defect.
        void Init(RHI::RHIContext& rhiCtx, const Descriptor& descriptor)
        {
            m_descriptor = descriptor;
            m_staging.resize(descriptor.m_capacity);

            RHI::BufferDescriptor bufDesc;
            bufDesc.m_byteCount = static_cast<uint64_t>(descriptor.m_capacity) * sizeof(Element);
            bufDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor       = bufDesc;
            init.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;

            m_buffer = rhiCtx.CreateEntity();
            rhiCtx.Add<RHI::PendingBufferInit>(m_buffer, init);
            rhiCtx.Add<RHI::PerFrameTag>(m_buffer);
            rhiCtx.Add<RHI::ResourceName>(m_buffer, RHI::ResourceName{ descriptor.m_resourceName });
        }

        void Shutdown(RHI::RHIContext& rhiCtx)
        {
            if (m_buffer != RHI::NullHandle)
            {
                rhiCtx.Add<DeadTag>(m_buffer);
                m_buffer = RHI::NullHandle;
            }
        }

        //! Re-points the SRV at this frame's copy. The descriptor encodes one buffer
        //! address and the copies live at distinct addresses, so this runs every frame.
        //!
        //! False means the buffer has not materialized yet. The caller must skip the
        //! whole frame's work, not just the upload — for a mirror filled incrementally,
        //! writing without uploading would leave the allocator and the GPU disagreeing.
        bool BindFrame(RHI::RHIContext& rhiCtx, uint32_t frameIndex)
        {
            if (m_buffer == RHI::NullHandle || m_descriptor.m_bindingsEntity == RHI::NullHandle)
            {
                return false;
            }

            auto* perFrame = rhiCtx.TryGet<RHI::Components::BufferPerFrame>(m_buffer);
            if (!perFrame || !perFrame->m_buffers[frameIndex])
            {
                return false;
            }

            const RHI::BufferViewDescriptor viewDesc = RHI::BufferViewDescriptor::CreateStructured(0, m_descriptor.m_capacity, sizeof(Element));
            RHI::BufferView* view = RHI::GetOrCreateBufferViewPerFrame(
                rhiCtx, m_buffer, *perFrame->m_buffers[frameIndex], viewDesc, frameIndex);
            if (!view)
            {
                LOG_ERROR("[StagedArrayBuffer] Failed to create structured SRV for '{}' frame {}.",
                          m_descriptor.m_resourceName.GetCStr(), frameIndex);
                return false;
            }

            SetShaderBuffer(m_descriptor.m_bindingsEntity, m_descriptor.m_inputName, view);
            return true;
        }

        //! Declarative whole-array copy of [0, count) into this frame's copy: the source
        //! stays the caller's memory and RHIResourceSystem does the copy at the next
        //! OnFrameBegin, so no mapped pointer ever escapes. count is the live length the
        //! caller's fill policy produced — dense count, high-water mark, or Capacity().
        void Upload(RHI::RHIContext& rhiCtx, uint32_t count)
        {
            if (count == 0)
            {
                return;
            }
            rhiCtx.AddOrReplace<RHI::PendingBufferMap>(
                m_buffer,
                RHI::PendingBufferMap{ m_staging.data(), 0,
                                       static_cast<size_t>(count) * sizeof(Element) });
        }

        //! Mirror access. Writing before BindFrame succeeds is safe but pointless — the
        //! staging is sized at Init, so it is a wasted write, not a fault.
        Element&       operator[](uint32_t index)       { return m_staging[index]; }
        const Element& operator[](uint32_t index) const { return m_staging[index]; }
        Element*       Data()                           { return m_staging.data(); }

        uint32_t          Capacity() const { return m_descriptor.m_capacity; }
        const ObjectName& Name() const     { return m_descriptor.m_resourceName; }

    private:
        Descriptor             m_descriptor;
        eastl::vector<Element> m_staging;
        RHI::RHIHandle         m_buffer = RHI::NullHandle;
    };
}
