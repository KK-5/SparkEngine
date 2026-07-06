#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::RHI
{
    class ImageView;
}

namespace Spark::Render
{
    //! Feeds the SkyboxPass through the normal DrawRequest → DrawItem path. Each frame
    //! it finds the ready environment cube (published by SkyboxSystem as
    //! Skybox::SkyboxGPUComponent on a world entity — read directly, the same way
    //! InstanceBindingSystem reads Mesh::MeshGPUComponent) and emits a DrawRequest that
    //! references the view (space0) + cube (space1) binding entities. It does NOT
    //! hand-assemble a DrawItem — CompileDrawRequests does that uniformly.
    //!
    //! The full-screen triangle is a procedural Drawable: DrawLinear(3), NoInstanceBinding,
    //! carrying the Drawable component but no DrawableTag so the generic assembly never
    //! sweeps it into other passes. The cube SRV/sampler in space1 are resource content:
    //! bound only when they change (sampler once; cube view on swap). No cube ready -> no
    //! DrawRequest -> the clear color shows.
    class SkyboxProcessor final
    {
    public:
        void Init();
        void Shutdown();
        void Process(const Math::Vector2Int& renderSize);

    private:
        RHI::RHIHandle m_cubeBindings   = RHI::NullHandle; //!< space1 SRG (g_SkyCube + g_SkySampler)
        RHI::RHIHandle m_drawableEntity = RHI::NullHandle; //!< procedural Drawable (component only, no DrawableTag)
        RHI::RHIHandle m_drawEntity     = RHI::NullHandle; //!< SkyboxPassTag + the emitted DrawRequest

        // Change-detection for the space1 resource content: rebind only on change.
        const RHI::ImageView* m_appliedCubeView = nullptr; //!< last cube view bound into space1
        bool                  m_samplerApplied  = false;   //!< constant sampler applied once
    };
}
