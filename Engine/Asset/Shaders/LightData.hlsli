// Single definition source for the per-light GPU record. Any shader needing a light
// record includes this (directly, or via SceneBindings.hlsli / Lib/Lights.hlsli).
// C++ mirror lives in Render/Binding/Scene/LightData.h — keep the two identical.
#ifndef SPARK_LIGHT_DATA_HLSLI
#define SPARK_LIGHT_DATA_HLSLI

static const uint LIGHT_TYPE_INVALID = 0u;
static const uint LIGHT_TYPE_DIRECTIONAL = 1u;
static const uint LIGHT_TYPE_POINT = 2u;
static const uint LIGHT_TYPE_SPOT = 3u;
static const uint LIGHT_TYPE_RECT = 4u;

// 96 bytes. A zeroed record has no light.
struct LightData
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

struct LightRecord
{
    LightData current;
    LightData previous;
};

#endif // SPARK_LIGHT_DATA_HLSLI
