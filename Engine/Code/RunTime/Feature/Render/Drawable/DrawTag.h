#pragma once

namespace Spark::Render
{
    //! Object classification tags — a property of the OBJECT that decides WHICH
    //! passes consume it (a Scope selects it via Accepts<ClassTag>()). A property
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

    //! Shadow dimension (optional, orthogonal). Routes to the shadow pass(es).
    struct ShadowCasterTag {};
}
