#pragma once

#include <RHI/Context/RHIHandle.h>

namespace Spark::Material
{
    //! Resolved base-color texture (GPU RHIHandle) on the material entity. Written by
    //! MaterialTextureSystem (owned by MaterialSystem), read by render's
    //! MaterialBindingSystem to resolve the bindless index. NullHandle = unresolved.
    //! This is the producer→consumer handoff component, so it lives in SparkMaterial
    //! (the producer's module); render depends on SparkMaterial and includes it.
    struct MaterialGPUTextures
    {
        RHI::RHIHandle m_baseColor = RHI::NullHandle;
    };
}
