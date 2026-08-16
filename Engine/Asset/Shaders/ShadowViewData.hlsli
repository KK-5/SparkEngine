// One entry of the global g_ShadowViews StructuredBuffer (space0), indexed by
// LightData::shadowIndex — the atlas tile slot, so the buffer has holes.
// C++ mirror lives in Render/SceneBind/ShadowViewData.h — keep the two identical.
#ifndef SPARK_SHADOW_VIEW_DATA_HLSLI
#define SPARK_SHADOW_VIEW_DATA_HLSLI

// 96 bytes.
struct ShadowViewData
{
    float4x4 worldToShadowUV;   // world -> atlas UV, tile transform folded in
    float4   uvMinMax;          // (minU, minV, maxU, maxV) of the tile's inset rect
    float    depthBias; float normalOffsetTexels; float pcfRadiusTexels; float texelWorldSizePerW;
};

#endif // SPARK_SHADOW_VIEW_DATA_HLSLI
