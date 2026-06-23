// Single definition source for the per-instance GPU record. Any shader needing
// per-instance data includes this (directly or via InstanceBindings.hlsl).
// C++ mirror lives in Render/Instance/InstanceData.h — keep the two identical.
#ifndef SPARK_INSTANCE_DATA_HLSLI
#define SPARK_INSTANCE_DATA_HLSLI

struct InstanceData
{
    float4x4 Model;   // object -> world
};

#endif // SPARK_INSTANCE_DATA_HLSLI
