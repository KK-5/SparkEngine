// Single definition source for the per-light GPU record. Any shader needing a light
// record includes this (directly, or via SceneBindings.hlsli / Lib/Lights.hlsli).
// C++ mirror lives in Render/Binding/Scene/LightData.h — keep the two identical.
#ifndef SPARK_LIGHT_DATA_HLSLI
#define SPARK_LIGHT_DATA_HLSLI

#include <Shaders/GeometryData.hlsli>

static const uint LIGHT_TYPE_INVALID = 0u;
static const uint LIGHT_TYPE_DIRECTIONAL = 1u;
static const uint LIGHT_TYPE_POINT = 2u;
static const uint LIGHT_TYPE_SPOT = 3u;
static const uint LIGHT_TYPE_RECT = 4u;
static const uint LIGHT_TYPE_EMISSIVE_MESH = 5u;
static const uint INVALID_LIGHT_SLOT = 0xffffffffu;

// 96 bytes. A zeroed record has no light.
struct LightData
{
    float3 color;
    uint type;
    uint4 typeData[4];
    uint3 typeDataTail;
    float intensity;
};

struct AnalyticLightData
{
    float3 direction; float intensity;   // direction the light shines (world); radiant intensity, or radiance for a rect
    float3 color;     uint  type;        // rgb and LIGHT_TYPE_*
    float3 position;  float invRange;    // world position, 1/range (0=dir)
    float  cosInner;  float cosOuter;                 // spot cone
    int    shadowIndex;                               // first shadow map and g_ShadowViews row; -1 = none
    uint   shadowMapCount;                            // >1 adds a cube face index to it
    float3 right;     int   shadowMaskIndex;          // rect/directional right axis; ShadowMask slot, -1 = none
    float  halfWidth; float halfHeight;               // rect, world units
    uint   castShadow;                               // authoring switch, independent of shadow-map allocation
    float  directionalConeHeight;                    // 1 - cos(source half-angle)
};

struct EmissiveLightData
{
    float4x4 model;
    GeometryData geometry;
    float3 color;
    float intensity;
    uint textureIndex;
};

bool IsAnalyticLight(uint type)
{
    return type >= LIGHT_TYPE_DIRECTIONAL && type <= LIGHT_TYPE_RECT;
}

AnalyticLightData DecodeAnalyticLight(LightData data)
{
    AnalyticLightData light;
    light.color = data.color;
    light.type = data.type;
    light.intensity = data.intensity;
    light.direction = asfloat(data.typeData[0].xyz);
    light.directionalConeHeight = asfloat(data.typeData[0].w);
    light.position = asfloat(data.typeData[1].xyz);
    light.invRange = asfloat(data.typeData[1].w);
    light.cosInner = asfloat(data.typeData[2].x);
    light.cosOuter = asfloat(data.typeData[2].y);
    light.shadowIndex = asint(data.typeData[2].z);
    light.shadowMapCount = data.typeData[2].w;
    light.right = asfloat(data.typeData[3].xyz);
    light.shadowMaskIndex = asint(data.typeData[3].w);
    light.halfWidth = asfloat(data.typeDataTail.x);
    light.halfHeight = asfloat(data.typeDataTail.y);
    light.castShadow = data.typeDataTail.z;
    return light;
}

EmissiveLightData DecodeEmissiveLight(LightData data)
{
    EmissiveLightData light;
    light.model = float4x4(asfloat(data.typeData[0]), asfloat(data.typeData[1]),
        asfloat(data.typeData[2]), float4(0.0, 0.0, 0.0, 1.0));
    light.geometry = (GeometryData)0;
    light.geometry.FirstIndex = data.typeData[3].x;
    light.geometry.VertexOffset = asint(data.typeData[3].y);
    light.geometry.IndexByteStride = data.typeData[3].z;
    light.geometry.TriangleCount = data.typeData[3].w;
    light.geometry.LocalSurfaceArea = asfloat(data.typeDataTail.z);
    light.color = data.color;
    light.intensity = data.intensity;
    light.textureIndex = data.typeDataTail.y;
    return light;
}

struct LightRecord
{
    LightData current;
    LightData previous;
};

#endif // SPARK_LIGHT_DATA_HLSLI
