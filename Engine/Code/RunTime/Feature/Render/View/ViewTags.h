#pragma once

namespace Spark::Render
{
    //! Compile-time tag identifying the main view's shader-binding entity in the
    //! RHIContext. View types are few and known at compile time, so consumers find
    //! the shared binding via GetView<MainViewTag, ...> — no runtime key lookup.
    //! Add ShadowViewTag / ReflectionViewTag etc. as more view types appear.
    struct MainViewTag {};
}
