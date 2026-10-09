// Single definition source for the per-light GPU record. Any shader needing a light
// record includes this (directly, or via SceneBindings.hlsli / Lib/Lights.hlsli).
// C++ mirror lives in Render/Binding/Scene/LightData.h — keep the two identical.
#ifndef SPARK_LIGHT_DATA_HLSLI
#define SPARK_LIGHT_DATA_HLSLI

// 96 bytes. type: 0=directional, 1=point, 2=spot, 3=rect.
struct LightData
{
    float3 direction; float intensity;   // direction the light shines (world); radiant intensity, or radiance for a rect
    float3 color;     uint  type;        // rgb; 0=directional, 1=point, 2=spot, 3=rect
    float3 position;  float invRange;    // world position, 1/range (0=dir)
    float  cosInner;  float cosOuter;                 // spot cone
    int    shadowIndex;                               // first g_ShadowViews row; -1 = none
    uint   shadowFaceCount;                           // >1 adds a cube face index to it
    float3 right;     int   shadowMaskIndex;          // rect unit right axis; ShadowMask slot, -1 = none
    float  halfWidth; float halfHeight;               // rect, world units
    // Scalars, not a uint2: a vector's alignment rule would decide where it lands and the
    // C++ mirror's would not agree.
    uint   padding0; uint padding1;
};

#endif // SPARK_LIGHT_DATA_HLSLI
