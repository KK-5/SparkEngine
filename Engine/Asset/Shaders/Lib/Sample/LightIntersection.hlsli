// Light discovery along a ray, separate from BRDFs, visibility and MIS.
#ifndef SPARK_LIB_LIGHT_INTERSECTION_HLSLI
#define SPARK_LIB_LIGHT_INTERSECTION_HLSLI

#include <Shaders/Lib/Geometry/Rectangle.hlsli>
#include <Shaders/Lib/Sample/LightSampling.hlsli>

// This adapter uses only the supplied light data. Rect lights emit on their
// authored front face, matching EvaluateLightSampleGeometry.
bool IntersectRectLight(RayDesc ray, LightData light, out RectangleHit hit)
{
    hit = (RectangleHit)0;
    if (light.type != 3u)
    {
        return false;
    }

    WorldRectangle rectangle;
    rectangle.Center = light.position;
    rectangle.Normal = light.direction;
    rectangle.Right = light.right;
    rectangle.HalfSize = float2(light.halfWidth, light.halfHeight);
    RectangleHit candidate;
    if (!IntersectRectangle(ray, rectangle, candidate) || !candidate.FrontFace)
    {
        return false;
    }
    hit = candidate;
    return true;
}

// Initialize nextLightIndex to 0 for each new ray. This is the candidate-search
// layer; a future light hierarchy can replace the scan without changing the
// rectangle intersection or LightSampleLocation representation.
bool FindNextRectLightCandidate(inout uint nextLightIndex, out uint lightIndex)
{
    lightIndex = 0u;
    [loop]
    while (nextLightIndex < g_LightCount)
    {
        const uint index = nextLightIndex++;
        if (GetLight(index).type == 3u)
        {
            lightIndex = index;
            return true;
        }
    }
    return false;
}

// Enumerates all front-facing rectangle hits in the caller's ray interval.
// Does not shorten TMax: analytic lights are not occluders of one another.
// Use the original receiver and sampled direction for this geometric event;
// visibility-ray offsets belong to a subsequent, separate query.
bool FindNextRectLightIntersection(RayDesc ray, inout uint nextLightIndex,
                                   out LightSampleLocation location, out float hitT)
{
    location = (LightSampleLocation)0;
    hitT = 0.0;
    uint lightIndex;
    [loop]
    while (FindNextRectLightCandidate(nextLightIndex, lightIndex))
    {
        RectangleHit hit;
        if (IntersectRectLight(ray, GetLight(lightIndex), hit))
        {
            location.Light.Source = LIGHT_SOURCE_SCENE;
            location.Light.Index = lightIndex;
            location.Coordinates = hit.Coordinates;
            hitT = hit.T;
            return true;
        }
    }
    return false;
}

#endif // SPARK_LIB_LIGHT_INTERSECTION_HLSLI
