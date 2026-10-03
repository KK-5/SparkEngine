// What the three GTAO passes share: XeGTAO's constants, filled from the view and the Scope.
// Include it after ScopeBindings.hlsli; the pass's ScopeParameters carries these fields, which
// AmbientOcclusionPass sets alike in all three:
//     uint   viewIndex;
//     uint   temporalNoise;   // 1 while something accumulates frames (temporal AA)
//     float2 tanHalfFov;      // of the view's projection
//     float  effectRadius;    // world units
//     float  intensity;
#ifndef SPARK_GTAO_COMMON_HLSLI
#define SPARK_GTAO_COMMON_HLSLI

#include <Shaders/ViewBindings.hlsli>
#include <Shaders/Lib/XeGTAO.hlsli>

// What the source's GTAOUpdateConstants computes on the CPU from the projection matrix.
GTAOConstants GetGTAOConstants()
{
    const ViewData view = GetView(g_Scope.viewIndex);

    GTAOConstants consts;
    consts.ViewportSize      = int2(view.bufferSizeAndInvSize.xy);
    consts.ViewportPixelSize = view.bufferSizeAndInvSize.zw;

    // ConvertFromDeviceZ's perspective branch, m32 / (deviceZ - m22), in the source's form.
    // The reversed-Z of the engine's projection is already in m22 and m32.
    consts.DepthUnpackConsts = float2(-view.invDeviceZToViewZ.y, view.invDeviceZToViewZ.x);

    consts.NDCToViewMul = float2(g_Scope.tanHalfFov.x * 2.0, g_Scope.tanHalfFov.y * -2.0);
    consts.NDCToViewAdd = float2(g_Scope.tanHalfFov.x * -1.0, g_Scope.tanHalfFov.y * 1.0);
    consts.NDCToViewMul_x_PixelSize = consts.NDCToViewMul * consts.ViewportPixelSize;

    consts.EffectRadius           = g_Scope.effectRadius;
    consts.FinalValuePower        = XE_GTAO_DEFAULT_FINAL_VALUE_POWER;
    consts.DenoiseBlurBeta        = XE_GTAO_DEFAULT_DENOISE_BLUR_BETA;
    consts.DepthMIPSamplingOffset = XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET;
    consts.FinalIntensity         = g_Scope.intensity;
    consts.NoiseIndex             = g_Scope.temporalNoise != 0 ? int(view.frameNumber % 64) : 0;
    return consts;
}

#endif // SPARK_GTAO_COMMON_HLSLI
