#pragma once

#include <EASTL/array.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/AccessFlags.h>
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

    //! Material images are sampled through the bindless heap.
    struct MaterialTextureTag
    {
        static constexpr RHI::AccessFlags Access = RHI::AccessFlags::ShaderSampledRead;
    };
}
