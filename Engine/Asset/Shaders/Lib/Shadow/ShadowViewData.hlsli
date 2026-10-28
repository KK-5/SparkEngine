// One entry of the global g_ShadowViews StructuredBuffer (space0), indexed by a shadow map's
// index — LightData::shadowIndex plus the light's map — so the buffer has holes.
// C++ mirror lives in Render/Binding/Scene/ShadowViewData.h — keep the two identical.
#ifndef SPARK_SHADOW_VIEW_DATA_HLSLI
#define SPARK_SHADOW_VIEW_DATA_HLSLI

// 96 bytes. Scalars after the matrix, not vectors: a vector's alignment rule would decide
// where it lands and the C++ mirror's would not agree.
struct ShadowViewData
{
    float4x4 worldToShadowUV;   // world -> UV of the shadow map's layer
    float    uvMax;             // the shadow map covers [0, uvMax] of its layer; < 0 for a hole
    float    depthBias; float normalOffsetTexels; float pcfRadiusTexels; float texelWorldSizePerW;
    float    padding0; float padding1; float padding2;
};

#endif // SPARK_SHADOW_VIEW_DATA_HLSLI
