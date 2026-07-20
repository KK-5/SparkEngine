#pragma once

#include <cstdint>

#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! Per-material derived component, placed on the MATERIAL entity (MaterialContext):
    //! this material's slot in the current frame's g_Materials buffer. Written every
    //! frame by MaterialBindingSystem's dense scatter (slot = iteration index) and read
    //! by InstanceBindingSystem to resolve InstanceData.m_materialIndex. Because the
    //! host buffer is re-scattered in full each frame, the slot reshuffles — it is
    //! rewritten every frame, not a stable id (analogous to InstanceSlotTable's slots).
    struct MaterialGPUSlot
    {
        uint32_t m_slot = 0;
    };

    //! Resolved base-color texture (GPU RHIHandle) on the material entity; written by
    //! MaterialTextureSystem, read by MaterialBindingSystem. NullHandle = unresolved.
    struct MaterialGPUTextures
    {
        RHI::RHIHandle m_baseColor = RHI::NullHandle;
    };

    //! Marks the single ShaderBindings entity (in the RHIContext) that holds the
    //! g_Materials SRV at space3. GBufferProcessor discovers it via
    //! AddShaderBindings<MaterialBindingTag> and injects it into draw requests, exactly
    //! like MainViewTag (space1 view) and InstanceBindingTag (space4 object).
    struct MaterialBindingTag {};
}
