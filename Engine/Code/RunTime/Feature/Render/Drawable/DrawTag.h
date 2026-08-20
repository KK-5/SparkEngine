#pragma once

namespace Spark::Render
{
    //! Object classification tags — a property of the OBJECT that decides WHICH
    //! passes consume it (each pass pulls via GetView<ClassTag, GeometrySpec>). A property
    //! that only changes the PSO within the same pass set (alpha-test discard, cull
    //! mode, …) is an object-side PSO hint, not a classification tag.
    //!
    //! Tags group into dimensions: exclusive within a dimension (composer derives one),
    //! orthogonal across (Opaque + ShadowCaster coexist and route to their own passes).

    //! Shading dimension (exclusive). Opaque/Mask alpha modes -> here; routes to the
    //! deferred geometry passes. Mask rides the same passes via a PSO hint.
    struct OpaqueTag {};

    //! Shading dimension (exclusive) — alpha-blended, forward-shaded. Not wired yet.
    struct TransparentTag {};

    //! Shadow dimension (optional, orthogonal). Routes to the shadow pass(es). Not wired yet.
    struct ShadowCasterTag {};

    //! Procedural full-screen-triangle dimension. ONE shared GeometrySpec (DrawLinear(3),
    //! no geometry / no per-object data) carries this; every full-screen pass declares
    //! .Accepts<FullScreenTriangleTag>() and stamps its tag on that one entity — no
    //! per-pass procedural spec needed.
    struct FullScreenTriangleTag {};
}
