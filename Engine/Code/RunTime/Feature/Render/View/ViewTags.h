#pragma once

namespace Spark::Render
{
    //! Compile-time tag identifying the main view's shader-binding entity in the
    //! RHIContext. View types are few and known at compile time, so consumers find
    //! the shared binding via GetView<MainViewTag, ...> — no runtime key lookup.
    //! Add ShadowViewTag / ReflectionViewTag etc. as more view types appear.
    struct MainViewTag {};

    //! Per-view visibility marker. A Drawable carrying Visible<V> passed culling for
    //! view V this frame and is therefore eligible for that view's pass assembly
    //! (AssembleDrawRequests scans DrawableTag + Visible<V>). Visibility is inherently
    //! per-view, so this is templated on the view tag from the start.
    //!
    //! Until a culling system exists, DrawableComposer stamps Visible<MainViewTag> on
    //! every Drawable it produces ("no culling yet = everything visible"); the culling
    //! system will later own the set/clear (per view). Procedural draws (skybox,
    //! full-screen FX) never enter this pipeline and manage their own DrawRequests, so
    //! they carry no Visible<V> and are naturally excluded from generic assembly.
    template <typename ViewTag>
    struct Visible {};
}
