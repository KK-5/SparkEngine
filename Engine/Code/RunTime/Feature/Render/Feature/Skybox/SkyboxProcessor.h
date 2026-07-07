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
        RHI::ImageView* GetCubeImageView();


        RHI::RHIHandle m_drawable    = RHI::NullHandle; //!< procedural Drawable (component only, no DrawableTag)
        RHI::RHIHandle m_drawRequest = RHI::NullHandle; //!< SkyboxPassTag + the emitted DrawRequest

        // The space1 cube SRG is tag-owned (no member handle) — created and bound via
        // SetPassShaderXxx, reaped centrally at teardown. The cube view's redundant
        // re-binds are dropped inside SetShaderImage, so no cached view is needed here.
        bool m_samplerApplied = false;   //!< constant sampler applied once
    };
}
