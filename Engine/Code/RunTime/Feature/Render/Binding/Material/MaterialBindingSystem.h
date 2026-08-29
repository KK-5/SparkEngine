#pragma once

#include <RHI/Context/RHIContext.h>

#include <Material/Components.h>

#include "MaterialBinding.h"

namespace Spark::Render
{
    //! Owns the g_Materials array (space3) + its ShaderBindings. Every material entity
    //! carrying Resource::StandardPBR gets a stable slot, and its parameters are encoded
    //! into that slot's g_Materials record.
    //!
    //! Slot allocation, encoding and upload are all GlobalBuffer's; this only supplies
    //! the space3 SRG and the per-material encode. Materials are few (KB-level), so the
    //! encode stays unconditional — no DirtyTag, see TODO_GlobalBufferUploadPlan.md §8.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked before
    //! InstanceBindingSystem so a material's slot exists before an instance bakes it.
    class MaterialBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! frameIndex is the in-flight slot (swap-chain GetCurrentImageIndex), used to
        //! pick this frame's g_Materials copy. RenderSystem::OnTick passes it.
        void Update(uint32_t frameIndex);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound on live materials. Overflow logs and drops the surplus
        //! (rendering is not blocked). 1024 * 68B = 68 KB per frame copy.
        static constexpr uint32_t Capacity = 1024;

        GlobalBuffer<Materials, MaterialData, Resource::StandardPBR> m_materials;

        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Materials @ space3
    };
}
