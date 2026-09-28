#pragma once

#include <cstdint>

#include <Math/Matrix4x4.h>
#include <Math/Vector4.h>

namespace Spark::Render
{
    //! One view's GPU record, a row of g_Views (space1) at the view's stable slot. HLSL
    //! mirror lives in ViewData.hlsli; the two MUST stay byte-for-byte identical.
    //!
    //! Encoded each frame by ViewBindingSystem from the View, its ViewHistory and the
    //! frame's time. Field meanings are documented once, on the HLSL side.
    struct ViewData
    {
        Math::Matrix4X4 m_viewProjection     = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_invViewProj        = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_view               = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_invView            = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_viewProjectionNoAA = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_prevViewProjection = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_clipToPrevClip     = Math::Matrix4X4Const::IDENTITY;

        Math::Vector4   m_temporalAAJitter          {0.0f, 0.0f, 0.0f, 0.0f};
        Math::Vector4   m_viewRectMin               {0.0f, 0.0f, 0.0f, 0.0f};
        Math::Vector4   m_inputViewRectMin          {0.0f, 0.0f, 0.0f, 0.0f};
        Math::Vector4   m_viewSizeAndInvSize        {0.0f, 0.0f, 0.0f, 0.0f};
        Math::Vector4   m_bufferSizeAndInvSize      {0.0f, 0.0f, 0.0f, 0.0f};
        Math::Vector4   m_inputBufferSizeAndInvSize {0.0f, 0.0f, 0.0f, 0.0f};
        Math::Vector4   m_invDeviceZToViewZ         {0.0f, 0.0f, 0.0f, 0.0f};

        float           m_exposure           = 1.0f;
        float           m_preExposure        = 1.0f;
        float           m_oneOverPreExposure = 1.0f;
        uint32_t        m_frameNumber        = 0;

        float           m_gameTime     = 0.0f;
        float           m_prevGameTime = 0.0f;
        float           m_deltaTime    = 0.0f;
        uint32_t        m_padding0     = 0;
    };

    static_assert(sizeof(ViewData) == 592,
        "ViewData must stay 592 bytes to match ViewData.hlsli.");
}
