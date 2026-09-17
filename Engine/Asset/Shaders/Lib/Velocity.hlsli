// Velocity is this frame's unjittered NDC position minus last frame's, so a pixel's previous
// position is ndc - velocity.
#ifndef SPARK_VELOCITY_HLSLI
#define SPARK_VELOCITY_HLSLI

// Where no geometry wrote Velocity. GBufferPass clears to it, so the two must match.
static const float kVelocityUnwritten = 65504.0;   // float16 max

// Past +-2 a position is already off screen, so the bound only keeps real values finite and
// below the sentinel; it never pulls an off-screen position back on.
static const float kVelocityMax = 1024.0;

float2 CalcVelocity(float4 clipPosition, float4 prevClipPosition)
{
    // Behind the camera last frame: there is no previous screen position.
    if (prevClipPosition.w <= 0.0)
    {
        return float2(kVelocityMax, kVelocityMax);
    }
    float2 velocity = clipPosition.xy / clipPosition.w - prevClipPosition.xy / prevClipPosition.w;
    return clamp(velocity, -kVelocityMax, kVelocityMax);
}

bool IsVelocityWritten(float2 velocity)
{
    return velocity.x < kVelocityUnwritten;
}

#endif // SPARK_VELOCITY_HLSLI
