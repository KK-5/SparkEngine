// Analytic rectangle geometry. No scene bindings, light evaluation or visibility.
#ifndef SPARK_LIB_RECTANGLE_HLSLI
#define SPARK_LIB_RECTANGLE_HLSLI

struct WorldRectangle
{
    float3 Center;
    float3 Normal;
    float3 Right;
    float2 HalfSize;
};

struct RectangleHit
{
    float T; // ray parameter; world distance only for a unit ray direction
    float2 Coordinates; // [0,1]^2 along Right and cross(Normal, Right)
    bool FrontFace;
};

// Normal and Right must be orthonormal; HalfSize must be positive. Both faces
// intersect: the caller decides which side emits or participates in a query.
bool IntersectRectangle(RayDesc ray, WorldRectangle rectangle, out RectangleHit hit)
{
    hit = (RectangleHit)0;
    if (any(rectangle.HalfSize <= 0.0))
    {
        return false;
    }

    const float denominator = dot(rectangle.Normal, ray.Direction);
    if (denominator == 0.0)
    {
        return false;
    }
    const float t = dot(rectangle.Center - ray.Origin, rectangle.Normal) / denominator;
    if (!isfinite(t) || t < ray.TMin || t > ray.TMax)
    {
        return false;
    }

    const float3 relative = ray.Origin + t * ray.Direction - rectangle.Center;
    const float3 up = cross(rectangle.Normal, rectangle.Right);
    const float2 position = float2(dot(relative, rectangle.Right), dot(relative, up));
    if (any(abs(position) > rectangle.HalfSize))
    {
        return false;
    }

    hit.T = t;
    hit.Coordinates = 0.5 * (position / rectangle.HalfSize + 1.0);
    hit.FrontFace = denominator < 0.0;
    return true;
}

#endif // SPARK_LIB_RECTANGLE_HLSLI
