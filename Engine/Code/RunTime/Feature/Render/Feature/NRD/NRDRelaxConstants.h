#pragma once

#include <cstdint>

namespace Spark::Render::NRDRelax
{
    struct ConstantField
    {
        const char* m_name;
        uint32_t m_byteOffset;
        uint32_t m_byteCount;
    };

    // NRD 4.18.0 RELAX_SHARED_CONSTANTS in RELAX_Config.hlsli, followed by the
    // two fields declared by both RELAX_Atrous and RELAX_AtrousSmem.
    inline constexpr ConstantField kFields[] = {
        { "gWorldToClip", 0, 64 },
        { "gWorldToClipPrev", 64, 64 },
        { "gWorldToViewPrev", 128, 64 },
        { "gWorldPrevToWorld", 192, 64 },

        { "gRotatorPre", 256, 16 },
        { "gFrustumRight", 272, 16 },
        { "gFrustumUp", 288, 16 },
        { "gFrustumForward", 304, 16 },
        { "gPrevFrustumRight", 320, 16 },
        { "gPrevFrustumUp", 336, 16 },
        { "gPrevFrustumForward", 352, 16 },
        { "gCameraDelta", 368, 16 },
        { "gMvScale", 384, 16 },
        { "gMvBias", 400, 16 },

        { "gJitter", 416, 8 },
        { "gResolutionScale", 424, 8 },
        { "gResourceSizeInv", 432, 8 },
        { "gResourceSize", 440, 8 },
        { "gRectSizeInv", 448, 8 },
        { "gRectSizePrev", 456, 8 },
        { "gResourceSizeInvPrev", 464, 8 },
        { "gPrintfAt", 472, 8 },
        { "gInputRectOrigin", 480, 8 },
        { "gOutputRectOrigin", 488, 8 },
        { "gDispatchInputRectOrigin", 496, 8 },
        { "gDispatchOutputRectOrigin", 504, 8 },
        { "gRectSize", 512, 8 },

        { "gSpecMaxAccumulatedFrameNum", 520, 4 },
        { "gSpecMaxFastAccumulatedFrameNum", 524, 4 },
        { "gDiffMaxAccumulatedFrameNum", 528, 4 },
        { "gDiffMaxFastAccumulatedFrameNum", 532, 4 },
        { "gDisocclusionThreshold", 536, 4 },
        { "gDisocclusionThresholdAlternate", 540, 4 },
        { "gCameraAttachedReflectionMaterialID", 544, 4 },
        { "gStrandMaterialID", 548, 4 },
        { "gStrandThickness", 552, 4 },
        { "gRoughnessFraction", 556, 4 },
        { "gSpecVarianceBoost", 560, 4 },
        { "gSplitScreen", 564, 4 },
        { "gDiffBlurRadius", 568, 4 },
        { "gSpecBlurRadius", 572, 4 },
        { "gDepthThreshold", 576, 4 },
        { "gLobeAngleFraction", 580, 4 },
        { "gSpecLobeAngleSlack", 584, 4 },
        { "gHistoryFixEdgeStoppingNormalPower", 588, 4 },
        { "gRoughnessEdgeStoppingRelaxation", 592, 4 },
        { "gNormalEdgeStoppingRelaxation", 596, 4 },
        { "gFastHistoryClampingSigmaScale", 600, 4 },
        { "gHistoryAccelerationAmount", 604, 4 },
        { "gHistoryResetTemporalSigmaScale", 608, 4 },
        { "gHistoryResetSpatialSigmaScale", 612, 4 },
        { "gHistoryResetAmount", 616, 4 },
        { "gDenoisingRange", 620, 4 },
        { "gSpecPhiLuminance", 624, 4 },
        { "gDiffPhiLuminance", 628, 4 },
        { "gDiffMaxLuminanceRelativeDifference", 632, 4 },
        { "gSpecMaxLuminanceRelativeDifference", 636, 4 },
        { "gLuminanceEdgeStoppingRelaxation", 640, 4 },
        { "gConfidenceDrivenRelaxationMultiplier", 644, 4 },
        { "gConfidenceDrivenLuminanceEdgeStoppingRelaxation", 648, 4 },
        { "gConfidenceDrivenNormalEdgeStoppingRelaxation", 652, 4 },
        { "gDebug", 656, 4 },
        { "gOrthoMode", 660, 4 },
        { "gUnproject", 664, 4 },
        { "gFrameRateScale", 668, 4 },
        { "gFrameRateScaleSmoothed", 672, 4 },
        { "gCheckerboardResolveAccumSpeed", 676, 4 },
        { "gHistoryFixFrameNum", 680, 4 },
        { "gHistoryFixBasePixelStride", 684, 4 },
        { "gHistoryFixAlternatePixelStride", 688, 4 },
        { "gHistoryFixAlternatePixelStrideMaterialID", 692, 4 },
        { "gHistoryThreshold", 696, 4 },
        { "gViewZScale", 700, 4 },
        { "gMinHitDistanceWeight", 704, 4 },
        { "gDiffMinMaterial", 708, 4 },
        { "gSpecMinMaterial", 712, 4 },
        { "gRoughnessEdgeStoppingEnabled", 716, 4 },
        { "gFrameIndex", 720, 4 },
        { "gDiffCheckerboard", 724, 4 },
        { "gSpecCheckerboard", 728, 4 },
        { "gHasHistoryConfidence", 732, 4 },
        { "gHasDisocclusionThresholdMix", 736, 4 },
        { "gResetHistory", 740, 4 },

        { "gStepSize", 744, 4 },
        { "gIsLastPass", 748, 4 },
    };

    inline constexpr uint32_t kConstantBufferSize = 752;
}
