#pragma once

namespace Spark::Render
{
    //! Compile-time tag identifying the main view's shader-binding entity in the
    //! RHIContext. View types are few and known at compile time, so consumers find
    //! the shared binding via GetView<MainViewTag, ...> — no runtime key lookup.
    //! Add ShadowViewTag / ReflectionViewTag etc. as more view types appear.
    struct MainViewTag {};

    //! Per-view visibility marker. A Drawable / DrawItem carrying Visible<V> passed
    //! culling for view V this frame. Visibility is inherently per-view, so this is
    //! templated on the view tag from the start.
    //!
    //! Until a culling system exists, DrawableComposer stamps Visible<MainViewTag> on the
    //! Drawables it produces and DeriveDrawItems on the DrawItems it derives ("no culling
    //! yet = everything visible"); the culling system will later own the set/clear.
    template <typename ViewTag>
    struct Visible {};
}
