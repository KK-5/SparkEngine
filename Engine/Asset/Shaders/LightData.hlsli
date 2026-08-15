// Single definition source for the per-light GPU record. Any shader needing a light
// record includes this (directly, or via SceneBindings.hlsl / Lib/Lights.hlsli).
// C++ mirror lives in Render/SceneBind/LightData.h — keep the two identical.
#ifndef SPARK_LIGHT_DATA_HLSLI
#define SPARK_LIGHT_DATA_HLSLI

// 64 bytes. type: 0=directional, 1=point, 2=spot.
struct LightData
{
    float3 direction; float intensity;   // dir/spot direction (world), radiant intensity
    float3 color;     uint  type;        // rgb; 0=directional, 1=point, 2=spot
    float3 position;  float invRange;    // point/spot world position, 1/range (0=dir)
    float  cosInner;  float cosOuter;                 // spot cone
    int    shadowIndex;                               // first g_ShadowViews row; -1 = none
    uint   shadowFaceCount;                           // >1 adds a cube face index to it
};

#endif // SPARK_LIGHT_DATA_HLSLI
