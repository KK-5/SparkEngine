#pragma once

namespace Spark::Render
{
    //! One-time setup for GBufferPass: creates its per-pass material sampler SRG (space2,
    //! g_MatSampler) up front so BindPassDrawItems can auto-inject it by PassTag. The
    //! GBuffer's DrawItems are derived from world Drawables (OpaqueTag) by DeriveDrawItems
    //! and their bindings/viewport filled by BindPassDrawItems — no per-frame work here.
    class GBufferProcessor final
    {
    public:
        void Init();
    };
}
