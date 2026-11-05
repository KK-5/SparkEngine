#pragma once

#include <RHI/Context/UniqueRHIHandle.h>
#include <RHI/Resource/AccessFlags.h>
#include <Resource/Image/ImageAsset.h>

namespace Spark::Render
{
    //! Shared data and identities consumed independently by scene rendering systems.
    //! Render-owned state on a world entity. Removing it retires the images; changing only
    //! SkyboxComponent's intensity never changes this state or uploads the images again.
    struct SkyboxGPUComponent
    {
        Ptr<Resource::ImageAsset> m_cubemapAsset;
        RHI::UniqueRHIHandle m_cubemap;
        RHI::UniqueRHIHandle m_irradiance;
        RHI::UniqueRHIHandle m_prefiltered;
    };

    //! On the active sky's RHI image, for SkyboxPass to import.
    struct ActiveSkyCubeTag {};

    //! On the active sky's IBL images, declared by passes binding the scene.
    struct SkyLightingTag
    {
        static constexpr RHI::AccessFlags Access = RHI::AccessFlags::ShaderSampledRead;
    };

    //! Common access declaration for all static scene lookup tables.
    struct SceneLookupTableTag
    {
        static constexpr RHI::AccessFlags Access = RHI::AccessFlags::ShaderSampledRead;
    };

    //! Individual table identities. Consumers find them in RHIContext, excluding DeadTag.
    struct BRDFLutTag {};
    struct LTC1Tag {};
    struct LTC2Tag {};
}
