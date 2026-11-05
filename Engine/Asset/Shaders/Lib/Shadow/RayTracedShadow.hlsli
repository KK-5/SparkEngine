// Inline ray traced visibility: one occlusion ray per pixel, hard shadow, 1 spp.
//
// The result lands in the ShadowMask, which is where the lighting reads visibility from, so
// nothing downstream knows whether the mask came from the atlas or from a trace. That is
// also where a denoiser will sit later: this file produces the raw 1 spp signal.
//
// Binds g_RayTracingScene through RayTracingSceneBindings.hlsli, which the pass that
// includes this must declare with Binds<RayTracingSceneBindingTag>.
#ifndef SPARK_LIB_RAY_TRACED_SHADOW_HLSLI
#define SPARK_LIB_RAY_TRACED_SHADOW_HLSLI

#include <Shaders/RayTracingSceneBindings.hlsli>

//! A directional light's occluders have no range, so the ray runs as far as the API allows.
static const float kShadowRayTMax = 1e30;

//! Self-intersection offset along the normal. A world position reconstructed from depth is
//! less exact than one a rasterizer produced, so if acne appears this is the knob.
static const float kShadowRayOriginBias = 1e-3;

//! The instance inclusion mask, tested against each instance's own mask: nonzero by bitwise
//! AND. The scene writes 0xFF on every instance that has a bottom level structure, so all
//! geometry casts today; narrowing this to bit 0 (shadow caster) waits for something that
//! has to be excluded -- translucent surfaces, particles.
static const uint kShadowRayInstanceMask = 0xFF;

//! True when nothing blocks L at worldPos.
//!
//! A surface facing away from the light counts as lit: its own contribution is zero, so the
//! ray would only cost time. N is the surface normal the ray leaves along and must be
//! normalized. The scene comes from this file's g_RayTracingScene rather than a parameter:
//! there is one TLAS per frame, and a RaytracingAccelerationStructure argument is the kind of
//! thing a compiler version decides to disagree about.
bool TraceDirectionalShadow(float3 worldPos, float3 N, float3 L)
{
    if (dot(N, L) <= 0.0)
    {
        return true;
    }

    RayDesc ray;
    ray.Origin    = worldPos + N * kShadowRayOriginBias;
    ray.Direction = L;
    ray.TMin      = 0.0;
    ray.TMax      = kShadowRayTMax;

    // ACCEPT_FIRST_HIT_AND_END_SEARCH: one occlusion test, so the first hit ends traversal
    // and a single Proceed is the whole query. Deliberately no CULL_BACK_FACING_TRIANGLES:
    // light must not leak through single sided geometry.
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_RayTracingScene, RAY_FLAG_NONE, kShadowRayInstanceMask, ray);
    query.Proceed();

    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

#endif // SPARK_LIB_RAY_TRACED_SHADOW_HLSLI
