#pragma once

#include <EASTL/array.h>
#include <RHI/Context/RHIHandle.h>
#include <Resource/Material/StandardPBR.h>

namespace Spark::Render
{
    //! Non-owning resolved texture slots on a material entity. The renderer's texture
    //! cache owns the images; MaterialBindingSystem queries this component directly.
    struct MaterialGPUTextures
    {
        eastl::array<RHI::RHIHandle, Resource::MaterialTexSlotCount> m_handles;

        MaterialGPUTextures()
        {
            m_handles.fill(RHI::NullHandle);
        }
    };

    //! A texture the material tables index. A pass reads them as one set, and each publishes
    //! how it is sampled (Bindless) on its own entity.
    struct MaterialTextureTag
    {
    };
}
