#ifndef SPARK_LIB_SCREEN_TRACE_HLSLI
#define SPARK_LIB_SCREEN_TRACE_HLSLI

// Marching a ray through the depth buffer, a level of the depth chain at a time: a step crosses
// a whole cell of the coarsest level whose closest depth the ray stays in front of. The
// traversal is ported from ffx_sssr.h of AMD's FidelityFX SSSR
// (https://github.com/GPUOpen-Effects/FidelityFX-SSSR):
//
//   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this
//   software and associated documentation files (the "Software"), to deal in the Software
//   without restriction, including without limitation the rights to use, copy, modify, merge,
//   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
//   to whom the Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or
//   substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
//   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
//   FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//
// Nothing here is bound: the textures and the view arrive as arguments. Screen UV is [0, 1]
// with y down; depth is the device's, reversed-Z (larger is closer).
//
// Changes from the source:
// - The chain is two textures. Level 0 is the scene depth, a cell per pixel; level k is mip
//   k - 1 of the HZB's closest chain, whose cells cover 2x2 pixels at mip 0. The HZB's sides
//   are powers of two, so a level's cells are exactly 2x2 of the level below, which the
//   source's own chain (the screen size halved and rounded) is not.
// - That chain is wider than the screen, so the ray lives in trace UV, of which the screen is
//   the corner ScreenTraceSpace::uvFactor.
// - The march ends as a miss where the ray leaves the screen or the depth range, and never
//   climbs past the last level; the source loads out of bounds there and leaves it to the hit
//   validation, which Vulkan does not define.
// - `hit` is whether the ray came down through the finest level. The source's valid_hit
//   compares the step count with its bound and is always true.
// - Only the reversed-Z branch (FFX_SSSR_INVERTED_DEPTH_RANGE) is kept, and a mirror ray: no
//   wave-occupancy exit.
// - The hit validation is split into the pieces a caller combines; what needs the GBuffer
//   stays with the caller.

#include <Shaders/ViewData.hlsli>

#define SCREEN_TRACE_FLOAT_MAX 3.402823466e+38

//! Where the march happens. Trace UV is a pixel coordinate over `size`.
struct ScreenTraceSpace
{
    float2 uvFactor;    // screen UV times this is trace UV: HZB::UvFactor
    float2 size;        // level 0 in pixels, were it as wide as the chain: twice the HZB's mip 0
    int2   depthLast;   // the last pixel of the scene depth
    int    levelLast;   // the coarsest level: the HZB's mip count
};

ScreenTraceSpace ScreenTrace_MakeSpace(float2 uvFactor, uint2 hzbSize, uint hzbMipCount, uint2 screenSize)
{
    ScreenTraceSpace space;
    space.uvFactor  = uvFactor;
    space.size      = float2(hzbSize * 2);
    space.depthLast = int2(screenSize) - 1;
    space.levelLast = int(hzbMipCount);
    return space;
}

float2 ScreenTrace_ScreenUVToNdc(float2 uv)  { return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0); }
float2 ScreenTrace_NdcToScreenUV(float2 ndc) { return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5); }

//! The ray leaving `worldPosition`, the surface point at `screenUV` with depth `deviceZ`, along
//! `worldDirection`: origin and direction in trace UV and device depth, where a straight line
//! of the world is still straight.
void ScreenTrace_MakeRay(
    ViewData view, ScreenTraceSpace space, float2 screenUV, float deviceZ, float3 worldPosition, float3 worldDirection,
    out float3 origin, out float3 direction)
{
    // A perspective clip w is the view depth. Half of it along the ray leaves the second point
    // in front of the eye even for a ray that comes straight back.
    const float  viewDepth = mul(view.viewProjection, float4(worldPosition, 1.0)).w;
    const float4 farClip   = mul(view.viewProjection, float4(worldPosition + worldDirection * (0.5 * viewDepth), 1.0));
    const float3 farNdc    = farClip.xyz / farClip.w;

    origin    = float3(screenUV * space.uvFactor, deviceZ);
    direction = float3(ScreenTrace_NdcToScreenUV(farNdc.xy) * space.uvFactor, farNdc.z) - origin;
}

//! The closest depth of the cell at `cell`, a position in cells of `level`.
float ScreenTrace_LoadDepth(
    Texture2D<float> sceneDepth, Texture2D<float> hzbClosest, ScreenTraceSpace space, float2 cell, int level)
{
    const int2 coord = int2(cell);
    if (level == 0)
    {
        return sceneDepth.Load(int3(min(coord, space.depthLast), 0));
    }
    return hzbClosest.Load(int3(coord, level - 1));
}

void ScreenTrace_InitialAdvanceRay(
    float3 origin, float3 direction, float3 invDirection, float2 levelSize, float2 levelSizeInv,
    float2 floorOffset, float2 uvOffset, out float3 position, out float currentT)
{
    const float2 cell = levelSize * origin.xy;

    // Intersect ray with the half box that is pointing away from the ray origin.
    float2 xyPlane = floor(cell) + floorOffset;
    xyPlane = xyPlane * levelSizeInv + uvOffset;

    // o + d * t = p' => t = (p' - o) / d
    const float2 t = xyPlane * invDirection.xy - origin.xy * invDirection.xy;
    currentT = min(t.x, t.y);
    position = origin + currentT * direction;
}

//! True when the ray crossed the cell without reaching its depth, so the next level up is
//! worth trying; false when it stopped at the depth, or was already behind it.
bool ScreenTrace_AdvanceRay(
    float3 origin, float3 direction, float3 invDirection, float2 cell, float2 levelSizeInv,
    float2 floorOffset, float2 uvOffset, float surfaceZ, inout float3 position, inout float currentT)
{
    // Create boundary planes
    float2 xyPlane = floor(cell) + floorOffset;
    xyPlane = xyPlane * levelSizeInv + uvOffset;
    const float3 boundaryPlanes = float3(xyPlane, surfaceZ);

    // Intersect ray with the half box that is pointing away from the ray origin.
    // o + d * t = p' => t = (p' - o) / d
    float3 t = boundaryPlanes * invDirection - origin * invDirection;

    // Prevent using z plane when shooting out of the depth buffer.
    t.z = direction.z < 0.0 ? t.z : SCREEN_TRACE_FLOAT_MAX;

    // Choose nearest intersection with a boundary.
    const float tMin = min(min(t.x, t.y), t.z);

    // Larger z means closer to the camera.
    const bool aboveSurface = surfaceZ < position.z;

    // Decide whether we are able to advance the ray until we hit the xy boundaries or if we had to clamp it at the surface.
    // We use the asuint comparison to avoid NaN / Inf logic, also we actually care about bitwise equality here to see if tMin is the t.z we fed into the min3 above.
    const bool skippedTile = asuint(tMin) != asuint(t.z) && aboveSurface;

    // Make sure to only advance the ray if we're still above the surface.
    currentT = aboveSurface ? tMin : currentT;

    // Advance ray
    position = origin + currentT * direction;

    return skippedTile;
}

//! Marches the ray of ScreenTrace_MakeRay until it passes behind the depth buffer, in at most
//! `stepCountMax` steps. Returns where it stopped, in trace UV and device depth; `hit` is
//! false when it left the screen or ran out of steps first. A hit only says the ray is behind
//! what the pixel shows: whether it is on that surface is the caller's to judge
//! (ScreenTrace_ThicknessConfidence).
float3 ScreenTrace_March(
    Texture2D<float> sceneDepth, Texture2D<float> hzbClosest, ScreenTraceSpace space,
    float3 origin, float3 direction, uint stepCountMax, out bool hit)
{
    float3 invDirection;
    invDirection.x = direction.x != 0.0 ? 1.0 / direction.x : SCREEN_TRACE_FLOAT_MAX;
    invDirection.y = direction.y != 0.0 ? 1.0 / direction.y : SCREEN_TRACE_FLOAT_MAX;
    invDirection.z = direction.z != 0.0 ? 1.0 / direction.z : SCREEN_TRACE_FLOAT_MAX;

    // Start on the level with highest detail.
    int level = 0;

    // Could recompute these every iteration, but it's faster to hoist them out and update them.
    float2 levelSize    = space.size;
    float2 levelSizeInv = rcp(levelSize);

    // Offset to the bounding boxes uv space to intersect the ray with the center of the next pixel.
    // This means we ever so slightly over shoot into the next region.
    float2 uvOffset = 0.005 / space.size;
    uvOffset.x = direction.x < 0.0 ? -uvOffset.x : uvOffset.x;
    uvOffset.y = direction.y < 0.0 ? -uvOffset.y : uvOffset.y;

    // Offset applied depending on current level to move the boundary to the left/right upper/lower border depending on ray direction.
    const float2 floorOffset = float2(direction.x < 0.0 ? 0.0 : 1.0, direction.y < 0.0 ? 0.0 : 1.0);

    // Initially advance ray to avoid immediate self intersections.
    float  currentT;
    float3 position;
    ScreenTrace_InitialAdvanceRay(
        origin, direction, invDirection, levelSize, levelSizeInv, floorOffset, uvOffset, position, currentT);

    for (uint i = 0; i < stepCountMax && level >= 0; ++i)
    {
        // Nothing lies past the screen or the depth range, and no texel is loaded there.
        if (any(position.xy < 0.0) || any(position.xy >= space.uvFactor) || position.z <= 0.0 || position.z >= 1.0)
        {
            break;
        }

        const float2 cell     = levelSize * position.xy;
        const float  surfaceZ = ScreenTrace_LoadDepth(sceneDepth, hzbClosest, space, cell, level);
        const bool   skipped  = ScreenTrace_AdvanceRay(
            origin, direction, invDirection, cell, levelSizeInv, floorOffset, uvOffset, surfaceZ, position, currentT);

        if (!skipped)
        {
            --level;
            levelSize    *= 2.0;
            levelSizeInv *= 0.5;
        }
        else if (level < space.levelLast)
        {
            ++level;
            levelSize    *= 0.5;
            levelSizeInv *= 2.0;
        }
    }

    hit = level < 0;
    return position;
}

//! How far a hit is trusted for the distance between where the ray stopped and the surface the
//! pixel there shows: 1 on the surface, 0 from `thickness` behind it. The depth buffer holds
//! the front of things only, so a ray behind one may have passed it by.
float ScreenTrace_ThicknessConfidence(float distanceBehind, float thickness)
{
    const float confidence = 1.0 - smoothstep(0.0, thickness, distanceBehind);
    return confidence * confidence;
}

//! Fades a hit out over the last 5% of the screen's height towards each border, where what a
//! ray would have hit next is off screen.
float ScreenTrace_BorderFade(float2 screenUV, float2 screenSize)
{
    const float2 width  = 0.05 * float2(screenSize.y / screenSize.x, 1.0);
    const float2 border = smoothstep(0.0, width, screenUV) * (1.0 - smoothstep(1.0 - width, 1.0, screenUV));
    return border.x * border.y;
}

#endif // SPARK_LIB_SCREEN_TRACE_HLSLI
