#pragma once

#include <RHI/Context/RHIHandle.h>

namespace Spark::RHI
{
    class ImageView;
}

namespace Spark::Render
{
    //! Drives the SkyboxPass. Init creates the pass's procedural full-screen Drawable —
    //! classified with SkyboxPass's own PassTag so DeriveDrawItems routes it to exactly
    //! this pass — and allocates its cube SRG (space2). Unlike the lighting/tonemap
    //! processors it keeps a per-frame Process: the environment cube is a static import
    //! (published by SkyboxSystem as a world component), not a graph transient, so its
    //! SRV/sampler are resolved here in OnTick rather than a Compile hook. BindPassDrawItems
    //! bakes the SRG pointer onto the DrawItem; Process refreshes its content in place.
    //!
    //! No cube ready -> g_SkyCube stays unbound and the sky renders black until it
    //! materializes. The Drawable is reaped by DrawableComposer, the SRG by
    //! ReapPassShaderBindings — hence no Shutdown.
    class SkyboxProcessor final
    {
    public:
        void Init();
        void Process();

    private:
        RHI::ImageView* GetCubeImageView();

        RHI::RHIHandle m_drawable = RHI::NullHandle; //!< procedural Drawable (DrawableTag + SkyboxPassTag)

        // The space2 cube SRG is tag-owned (no member handle) — allocated in Init, bound
        // via SetPassShaderXxx, reaped centrally at teardown. The cube view's redundant
        // re-binds are dropped inside SetShaderImage, so no cached view is needed here.
        bool m_samplerApplied = false;   //!< constant sampler applied once
    };
}
