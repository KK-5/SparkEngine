#pragma once

#include <ECS/ComponentTraits.h>
#include <Resource/AssetTypes.h>
#include <Resource/Image/ImageAsset.h>

#include <RHI/Context/RHIHandle.h>

namespace Spark::Skybox
{
    //! User-facing skybox data on a world entity (mirrors MeshComponent). Pure
    //! data — just the environment image asset id (an HDRI today; its compiled
    //! form is a cubemap). The component does NOT load: asset loading is the upper
    //! layer's responsibility (the editor assigns an already-loaded asset). The
    //! system only organizes the loaded asset into GPU resources.
    //!
    //! Also deliberately BAKE-AGNOSTIC: it only ever references a cubemap-shaped
    //! image asset; the equirect→cubemap bake belongs to the asset / Image-Compile
    //! layer, not here.
    struct SkyboxComponent
    {
        Resource::AssetId m_imageAssetId;

        //! Multiplies the environment's radiance -- the visible sky AND both IBL terms,
        //! which is what keeps them consistent: scaling incident radiance by k scales the
        //! irradiance and prefiltered integrals by exactly k (both are linear in it), so
        //! this is equivalent to re-baking a k-times brighter HDRI, with no re-bake.
        //! Applied at sample time by the skybox and lighting shaders.
        float m_intensity = 1.0f;
    };

    //! Resolved GPU side, written by SkyboxSystem (mirrors MeshGPUComponent).
    //!  - m_cubemapAsset : reserved handle on the compiled cubemap image asset
    //!                     (the source the upload reads from). Today it is just the
    //!                     loaded environment asset; once the asset-layer bake lands
    //!                     it is the cubemap-form product — downstream is unchanged.
    //!  - m_cubemap      : the GPU texture the render SkyboxPass samples.
    //!  - m_irradiance /
    //!    m_prefiltered  : the environment bake's IBL products, uploaded as static
    //!                     imports (no render-graph attachment — SceneBindingSystem
    //!                     binds them into space0 and samples them every frame).
    //!                     NullHandle when the asset carries no IBL products.
    //! No equirect, no bake state: the feature does not know a bake happened.
    struct SkyboxGPUComponent
    {
        Ptr<Resource::ImageAsset> m_cubemapAsset;
        RHI::RHIHandle            m_cubemap     = RHI::NullHandle;
        RHI::RHIHandle            m_irradiance  = RHI::NullHandle;
        RHI::RHIHandle            m_prefiltered = RHI::NullHandle;
    };

    //! Placed on the RHIContext cube resource entity (SkyboxGPUComponent::m_cubemap) of the
    //! ACTIVE skybox. SkyboxSystem tags it at creation; the render SkyboxPass reads it to
    //! import the cube as its g_SkyCube shader-read attachment. Defined here (SparkSkybox),
    //! NOT in SparkRender, so the feature can set it without a feature->render dependency —
    //! the render pass reads it in the allowed render->feature direction. Singleton:
    //! SkyboxSystem clears any prior cube's tag before tagging a newly built one.
    //!
    //! Also the answer to "which skybox is active" for consumers that need the whole set:
    //! SceneBindingSystem finds the SkyboxGPUComponent whose m_cubemap carries this tag and
    //! takes its IBL handles from that same component, so sky and environment lighting can
    //! never come from two different HDRIs. The IBL images get no tag of their own -- they
    //! are static imports bound straight into space0, never render-graph attachments.
    struct ActiveSkyCubeTag {};
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Skybox::SkyboxComponent,
        static constexpr bool editable = true;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    )
}
