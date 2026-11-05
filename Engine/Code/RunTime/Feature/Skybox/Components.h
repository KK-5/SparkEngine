#pragma once

#include <ECS/ComponentTraits.h>
#include <Resource/AssetTypes.h>

namespace Spark::Skybox
{
    //! Scene description only. Asset loading belongs to the upper layer; consumers decide
    //! how to use the compiled cubemap and its optional environment-lighting products.
    struct SkyboxComponent
    {
        Resource::AssetId m_imageAssetId;
        //! Scales both the visible sky and its environment lighting, without changing the asset.
        float m_intensity = 1.0f;
    };

    //! Runtime selection assigned by SkyboxSystem to the first created skybox.
    struct ActiveSkyboxTag {};
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Skybox::SkyboxComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    )
}
