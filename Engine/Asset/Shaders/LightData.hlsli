// Single definition source for the per-light GPU record. Any shader needing a light
// record includes this (directly, or via SceneBindings.hlsli / Lib/Lights.hlsli).
// C++ mirror lives in Render/Binding/Scene/LightData.h — keep the two identical.
#ifndef SPARK_LIGHT_DATA_HLSLI
#define SPARK_LIGHT_DATA_HLSLI

// 80 bytes. type: 0=directional, 1=point, 2=spot.
struct LightData
{
    float3 direction; float intensity;   // dir/spot direction (world), radiant intensity
    float3 color;     uint  type;        // rgb; 0=directional, 1=point, 2=spot
    float3 position;  float invRange;    // point/spot world position, 1/range (0=dir)
    float  cosInner;  float cosOuter;                 // spot cone
    int    shadowIndex;                               // first g_ShadowViews row; -1 = none
    uint   shadowFaceCount;                           // >1 adds a cube face index to it
    int    shadowMaskIndex;                           // ShadowMask slot; -1 = none
    // Three scalars, not a uint3: a vector's alignment rule would decide where it lands and
    // the C++ mirror's would not agree.
    uint   padding0; uint padding1; uint padding2;
};

#endif // SPARK_LIGHT_DATA_HLSLI
