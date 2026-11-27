// Single definition source for the per-instance GPU record. Any shader needing
// per-instance data includes this (directly or via InstanceBindings.hlsli).
// C++ mirror lives in Render/Binding/Instance/InstanceData.h — keep the two identical (224 bytes).
#ifndef SPARK_INSTANCE_DATA_HLSLI
#define SPARK_INSTANCE_DATA_HLSLI

struct InstanceData
{
    float4x4 Model;          // object -> world
    float4x4 PrevModel;      // last frame's Model
    float4x4 NormalMatrix;   // inverse-transpose of Model; use (float3x3) for normals
    uint     MaterialIndex;  // slot into g_Materials (space3)
    uint     DrawMask;       // one bit per classification tag (DrawMask.h); 0 = not drawn
    uint     GeometryIndex;  // slot into g_Geometries (space6); only meaningful while DrawMask is set
    uint     PipelineKeyIndex; // what each pass's bin table is addressed by (PipelineKeyIndex.h)
    uint     MotionHistoryValid; // 0 on first appearance, missing history or explicit reset
    // Scalars avoid a uint3 member's stricter alignment on other backends.
    uint     LightSlotId;   // current emissive entry in g_Lights; 0xffffffff when absent
    uint     Padding1;
    uint     Padding2;
};

#endif // SPARK_INSTANCE_DATA_HLSLI
