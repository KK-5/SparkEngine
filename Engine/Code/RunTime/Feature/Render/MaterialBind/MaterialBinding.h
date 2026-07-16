#pragma once

#include <cstdint>

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

    //! Marks the single ShaderBindings entity (in the RHIContext) that holds the
    //! g_Materials SRV at space2. GBufferProcessor discovers it via
    //! AddShaderBindings<MaterialBindingTag> and injects it into draw requests, exactly
    //! like MainViewTag (space0 view) and InstanceBindingTag (space1 instance).
    struct MaterialBindingTag {};
}
