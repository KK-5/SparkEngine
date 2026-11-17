// Velocity / ResolvedVelocity (RGBA16F): RG = current minus previous unjittered NDC;
// B = previousViewZ - currentViewZ at the corresponding surface point; A = valid
// surface depth motion. Sky has A=0 and rotation-only RG motion. Consumers using
// only RG retain the original 2D motion convention.
#ifndef SPARK_VELOCITY_HLSLI
#define SPARK_VELOCITY_HLSLI

// Where no geometry wrote Velocity. GBufferPass clears to it, so the two must match.
static const float kVelocityUnwritten = 65504.0;   // float16 max

// Past +-2 a position is already off screen, so the bound only keeps real values finite and
// below the sentinel; it never pulls an off-screen position back on.
static const float kVelocityMax = 1024.0;
static const float kVelocityDepthMax = 65504.0; // finite range of the FP16 B channel

float2 CalcVelocity(float4 clipPosition, float4 prevClipPosition)
{
    // An invalid homogeneous position has no usable previous screen position.
    if (!all(isfinite(clipPosition)) || !all(isfinite(prevClipPosition))
        || clipPosition.w <= 0.0 || prevClipPosition.w <= 0.0)
    {
        return float2(kVelocityMax, kVelocityMax);
    }
    float2 velocity = clipPosition.xy / clipPosition.w - prevClipPosition.xy / prevClipPosition.w;
    return clamp(velocity, -kVelocityMax, kVelocityMax);
}

// viewDepths = (currentViewZ, previousViewZ), interpolated using CURRENT raster
// barycentrics. Preserve homogeneous clip positions until this pixel-stage divide.
float4 CalcSurfaceVelocity(float4 clipPosition, float4 prevClipPosition, float2 viewDepths, bool historyValid)
{
    const float2 velocity = CalcVelocity(clipPosition, prevClipPosition);
    float4 result = float4(velocity, 0.0, 0.0);
    if (!historyValid || !all(isfinite(viewDepths)) || any(viewDepths <= 0.0)
        || !all(isfinite(velocity)) || any(abs(velocity) >= kVelocityMax)
        || prevClipPosition.z <= 0.0 || prevClipPosition.z > prevClipPosition.w)
    {
        return result;
    }
    const float depthDelta = viewDepths.y - viewDepths.x;
    if (!isfinite(depthDelta) || abs(depthDelta) > kVelocityDepthMax)
    {
        return result;
    }
    result.zw = float2(depthDelta, 1.0);
    return result;
}

bool IsVelocityDepthValid(float4 velocity)
{
    return isfinite(velocity.w) && velocity.w > 0.5
        && isfinite(velocity.z) && abs(velocity.z) <= kVelocityDepthMax;
}

// Round-to-nearest FP16 error: half a subnormal step, or <= 2^-11 relative
// error for normal values. Include it when reconstructing previous view depth.
float VelocityDepthQuantizationError(float depthDelta)
{
    return max(abs(depthDelta) * (1.0 / 2048.0), 1.0 / 33554432.0);
}

bool IsVelocityWritten(float2 velocity)
{
    return velocity.x < kVelocityUnwritten;
}

//! Where the surface at screen UV `uv` (y down) was last frame, given its velocity. It may lie
//! off screen.
float2 PreviousScreenUV(float2 uv, float2 velocity)
{
    // Velocity is an NDC delta; NDC y points up, UV y down.
    return uv - velocity * float2(0.5, -0.5);
}

#endif // SPARK_VELOCITY_HLSLI
