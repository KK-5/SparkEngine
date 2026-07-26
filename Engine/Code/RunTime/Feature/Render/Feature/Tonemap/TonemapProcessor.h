#pragma once

#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! One-time setup for the final TonemapPass. Creates the pass's procedural full-screen
    //! Drawable — classified with TonemapPass's own PassTag so DeriveDrawItems routes it to
    //! exactly this pass — and allocates its SceneColor SRG (space2) up front so
    //! BindPassDrawItems can resolve it from frame one. Per-frame binding + viewport are
    //! handled by BindPassDrawItems; the SceneColor slot by TonemapPass's Compile hook. No
    //! per-frame work: the Drawable is reaped by DrawableComposer, the SRG by
    //! ReapPassShaderBindings.
    class TonemapProcessor
    {
    public:
        void Init();

    private:
        RHI::RHIHandle m_drawable = RHI::NullHandle;
    };
}
