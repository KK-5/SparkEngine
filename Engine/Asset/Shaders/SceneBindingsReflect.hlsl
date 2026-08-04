// Reflection host for the SceneBindings group.
//
// SceneBindings.hlsl is a pure resource/cbuffer header with no entry point, so the asset
// builder cannot compile/reflect it alone. This file gives the group a dummy vertex entry
// that references EVERY resource and constant in it, purely so SceneBindingSystem can
// reflect the space0 layout. It is NEVER used to render.
//
// "Every" is load-bearing: DXC drops whatever the entry point does not touch, and a dropped
// resource reflects as absent. Adding a resource to SceneBindings.hlsl without adding a use
// here is a silent no-op — no compile error anywhere.
//
// NOTE: the stage detector is a plain substring scan of the source, so this comment
// deliberately avoids spelling out the other entry-point names.
#include <Shaders/SceneBindings.hlsl>

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    LightData l = GetLight(0);
    float w = (float)g_LightCount;

    const float3 dir = float3(0.0, 1.0, 0.0);
    float3 env = g_IrradianceCube.SampleLevel(g_IBLSampler, dir, 0).rgb
               + g_PrefilteredCube.SampleLevel(g_IBLSampler, dir, 0).rgb;
    float2 dfg = g_BRDFLut.SampleLevel(g_IBLSampler, float2(1.0, 0.0), 0).rg;
    float  envScalar = g_EnvIntensity + (float)g_IBLPrefilteredMipCount + dfg.x + dfg.y;

    return float4(l.direction + l.color + l.position + env,
                  l.intensity + w + envScalar) * 1e-6;
}
