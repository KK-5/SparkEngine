#pragma once

namespace Spark::Render
{
    //! Compile-time tag identifying a view TYPE — "every main-camera view" — not one
    //! specific entity. View types are few and known at compile time; the number of
    //! view INSTANCES of a type is a runtime property (one camera or two in split
    //! screen, N faces for shadows), so instances are entities carrying this tag and
    //! consumers enumerate them via GetView<MainViewTag, ...>.
    //! Add ShadowViewTag / ReflectionViewTag etc. as more view types appear.
    struct MainViewTag {};
}
