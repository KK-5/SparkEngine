#pragma once

#include <cstdint>

namespace Spark::Render::NRDReblur
{
    struct ConstantField
    {
        const char* m_name;
        uint32_t m_byteCount;
    };

    // NRD 4.18.0 REBLUR_SHARED_CONSTANTS in REBLUR_Config.hlsli.
    // The seven enabled stages use this block. Its fields are contiguous in the SDK's packing.
    inline constexpr ConstantField kSharedFields[] = {
        { "gWorldToClip", 64 },
        { "gViewToClip", 64 },
        { "gViewToWorld", 64 },
        { "gWorldToViewPrev", 64 },
        { "gWorldToClipPrev", 64 },
        { "gWorldPrevToWorld", 64 },

        { "gRotatorPre", 16 },
        { "gRotator", 16 },
        { "gRotatorPost", 16 },
        { "gFrustum", 16 },
        { "gFrustumPrev", 16 },
        { "gCameraDelta", 16 },
        { "gHitDistSettings", 16 },
        { "gViewVectorWorld", 16 },
        { "gViewVectorWorldPrev", 16 },
        { "gMvScale", 16 },
        { "gMvBias", 16 },
        { "gConvergenceSettings", 16 },

        { "gAntilagSettings", 8 },
        { "gResourceSize", 8 },
        { "gResourceSizeInv", 8 },
        { "gResourceSizeInvPrev", 8 },
        { "gRectSize", 8 },
        { "gRectSizeInv", 8 },
        { "gRectSizePrev", 8 },
        { "gResolutionScale", 8 },
        { "gResolutionScalePrev", 8 },
        { "gJitter", 8 },
        { "gPrintfAt", 8 },
        { "gInputRectOrigin", 8 },
        { "gOutputRectOrigin", 8 },
        { "gDispatchInputRectOrigin", 8 },
        { "gDispatchOutputRectOrigin", 8 },
        { "gRectSizeMinusOne", 8 },

        { "gDisocclusionThreshold", 4 },
        { "gDisocclusionThresholdAlternate", 4 },
        { "gCameraAttachedReflectionMaterialID", 4 },
        { "gStrandMaterialID", 4 },
        { "gStrandThickness", 4 },
        { "gStabilizationStrength", 4 },
        { "gDebug", 4 },
        { "gOrthoMode", 4 },
        { "gUnproject", 4 },
        { "gDenoisingRange", 4 },
        { "gPlaneDistSensitivity", 4 },
        { "gFrameRateScale", 4 },
        { "gFrameRateScaleSmoothed", 4 },
        { "gMinBlurRadius", 4 },
        { "gMaxBlurRadius", 4 },
        { "gDiffPrepassBlurRadius", 4 },
        { "gSpecPrepassBlurRadius", 4 },
        { "gMaxAccumulatedFrameNum", 4 },
        { "gMaxFastAccumulatedFrameNum", 4 },
        { "gAntiFirefly", 4 },
        { "gLobeAngleFraction", 4 },
        { "gRoughnessFraction", 4 },
        { "gHistoryFixFrameNum", 4 },
        { "gHistoryFixBasePixelStride", 4 },
        { "gHistoryFixAlternatePixelStride", 4 },
        { "gHistoryFixAlternatePixelStrideMaterialID", 4 },
        { "gFastHistoryClampingSigmaScale", 4 },
        { "gMinRectDimMulUnproject", 4 },
        { "gUsePrepassNotOnlyForSpecularMotionEstimation", 4 },
        { "gSplitScreen", 4 },
        { "gSplitScreenPrev", 4 },
        { "gCheckerboardResolveAccumSpeed", 4 },
        { "gViewZScale", 4 },
        { "gFireflySuppressorMinRelativeScale", 4 },
        { "gMinHitDistanceWeight", 4 },
        { "gDiffMinMaterial", 4 },
        { "gSpecMinMaterial", 4 },
        { "gResponsiveAccumulationInvRoughnessThreshold", 4 },
        { "gResponsiveAccumulationMinAccumulatedFrameNum", 4 },
        { "gHasHistoryConfidence", 4 },
        { "gHasDisocclusionThresholdMix", 4 },
        { "gDiffCheckerboard", 4 },
        { "gSpecCheckerboard", 4 },
        { "gFrameIndex", 4 },
        { "gIsRectChanged", 4 },
        { "gResetHistory", 4 },
        { "gReturnHistoryLengthInsteadOfOcclusion", 4 },
    };
}
