#pragma once

namespace Spark::Render
{
    //! Compile-time tag identifying a view TYPE — "every main-camera view" — not one
    //! specific entity. View types are few and known at compile time; the number of
    //! view INSTANCES of a type is a runtime property (one camera or two in split
    //! screen, N shadow maps a light), so instances are entities carrying this tag and
    //! consumers enumerate them via GetView<MainViewTag, ...>.
    //! Add ReflectionViewTag etc. as more view types appear.
    struct MainViewTag {};

    //! Where a main view's image lands in the displayed target (swap chain): the editor
    //! viewport panel, or the whole window. Passes writing that target render this type.
    struct OutputViewTag {};

    //! Every shadow-casting view: one per directional / spot light, six per point light.
    //! Its shadow map is its light's ShadowMaps::First() plus its place in ShadowViewRefs.
    struct ShadowViewTag {};

    //! The view exists but must not be rendered this frame. Applies to any view type —
    //! a hidden editor viewport and a reflection probe running at reduced rate are the
    //! same statement.
    struct ViewInactiveTag {};
}
