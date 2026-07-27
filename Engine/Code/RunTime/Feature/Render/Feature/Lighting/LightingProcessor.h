#pragma once

#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! One-time setup for the deferred LightingPass. Creates the pass's procedural
    //! full-screen Drawable — classified with LightingPass's own PassTag so the generic
    //! DrawItemRouter routes it to exactly this pass — and allocates its GBuffer SRG
    //! (space2) up front so BindPassDrawItems can resolve it from frame one. Per-frame
    //! binding + viewport are handled by BindPassDrawItems; the GBuffer texture slots by
    //! LightingPass's Compile hook. No per-frame work, hence no Process/Shutdown: the
    //! Drawable is reaped by DrawItemRouter, the SRG by ReapPassShaderBindings.
    class LightingProcessor
    {
    public:
        void Init();

    private:
        RHI::RHIHandle m_drawable = RHI::NullHandle;
    };
}
