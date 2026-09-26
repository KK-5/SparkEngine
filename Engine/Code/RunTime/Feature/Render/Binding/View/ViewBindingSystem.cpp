#include "ViewBindingSystem.h"

#include <cmath>

#include <EASTL/fixed_vector.h>

#include <CoreComponents/Tags.h>
#include <Math/MathUtils.h>
#include <Math/Vector4.h>

#include <Shader/ShaderBindingsUtils.h>
#include <View/View.h>
#include <View/ViewComponents.h>

namespace Spark::Render
{
    namespace
    {
        Math::Vector4 SizeAndInvSize(float width, float height)
        {
            return Math::Vector4(width, height,
                width  > 0.0f ? 1.0f / width  : 0.0f,
                height > 0.0f ? 1.0f / height : 0.0f);
        }

        //! Names match ViewBindings.hlsli.
        void WriteViewConstants(const View& view, const ViewHistory& previous, const FrameTime& time,
            RHI::RHIHandle bindings)
        {
            const Math::Matrix4X4 viewProjection     = view.GetJitteredWorldToClip();
            const Math::Matrix4X4 viewProjectionNoAA = view.GetWorldToClip();
            const Math::Matrix4X4 prevViewProjection = previous.m_viewToClip * previous.m_worldToView;

            const float bufferWidth  = static_cast<float>(view.m_bufferSize.x);
            const float bufferHeight = static_cast<float>(view.m_bufferSize.y);
            const float viewWidth    = bufferWidth  * (view.m_rect.m_maxX - view.m_rect.m_minX);
            const float viewHeight   = bufferHeight * (view.m_rect.m_maxY - view.m_rect.m_minY);

            SetShaderConstant(bindings, RHI::InputName("g_ViewProjection"),     viewProjection);
            SetShaderConstant(bindings, RHI::InputName("g_InvViewProj"),        Math::Inverse(viewProjection));
            SetShaderConstant(bindings, RHI::InputName("g_View"),               view.m_worldToView);
            SetShaderConstant(bindings, RHI::InputName("g_InvView"),            Math::Inverse(view.m_worldToView));
            SetShaderConstant(bindings, RHI::InputName("g_ViewProjectionNoAA"), viewProjectionNoAA);
            SetShaderConstant(bindings, RHI::InputName("g_PrevViewProjection"), prevViewProjection);
            SetShaderConstant(bindings, RHI::InputName("g_ClipToPrevClip"),
                prevViewProjection * Math::Inverse(viewProjectionNoAA));

            SetShaderConstant(bindings, RHI::InputName("g_TemporalAAJitter"),
                Math::Vector4(view.m_jitter.x, view.m_jitter.y, previous.m_jitter.x, previous.m_jitter.y));
            // Rounded: a fraction times the buffer size can land a hair off a whole pixel.
            SetShaderConstant(bindings, RHI::InputName("g_ViewRectMin"), Math::Vector4(
                std::round(bufferWidth * view.m_rect.m_minX), std::round(bufferHeight * view.m_rect.m_minY), 0.0f, 0.0f));
            const bool ownInput = view.m_inputBufferSize == Math::Vector2Int(0, 0);
            const ViewRect& inputRect = ownInput ? view.m_rect : view.m_inputRect;
            const float inputWidth  = ownInput ? bufferWidth  : static_cast<float>(view.m_inputBufferSize.x);
            const float inputHeight = ownInput ? bufferHeight : static_cast<float>(view.m_inputBufferSize.y);
            SetShaderConstant(bindings, RHI::InputName("g_InputViewRectMin"), Math::Vector4(
                std::round(inputWidth * inputRect.m_minX), std::round(inputHeight * inputRect.m_minY), 0.0f, 0.0f));
            SetShaderConstant(bindings, RHI::InputName("g_ViewSizeAndInvSize"),   SizeAndInvSize(viewWidth, viewHeight));
            SetShaderConstant(bindings, RHI::InputName("g_BufferSizeAndInvSize"), SizeAndInvSize(bufferWidth, bufferHeight));
            SetShaderConstant(bindings, RHI::InputName("g_InputBufferSizeAndInvSize"), SizeAndInvSize(inputWidth, inputHeight));
            SetShaderConstant(bindings, RHI::InputName("g_InvDeviceZToViewZ"),
                Math::DeviceZToViewZParams(view.m_viewToClip));

            SetShaderConstant(bindings, RHI::InputName("g_Exposure"),     view.m_exposure);

            // P3's EyeAdaptation writes this from last frame's measured exposure; until
            // then it is 1, so the multiply and divide cancel exactly.
            constexpr float preExposure = 1.0f;
            SetShaderConstant(bindings, RHI::InputName("g_PreExposure"),        preExposure);
            SetShaderConstant(bindings, RHI::InputName("g_OneOverPreExposure"), 1.0f / preExposure);

            SetShaderConstant(bindings, RHI::InputName("g_FrameNumber"),  static_cast<uint32_t>(time.m_frameNumber));
            SetShaderConstant(bindings, RHI::InputName("g_GameTime"),     static_cast<float>(time.m_gameTime));
            SetShaderConstant(bindings, RHI::InputName("g_PrevGameTime"), static_cast<float>(time.m_prevGameTime));
            SetShaderConstant(bindings, RHI::InputName("g_DeltaTime"),    time.m_deltaTime);
        }
    }

    void ViewBindingSystem::Update(const FrameTime& time)
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        eastl::fixed_vector<RHI::RHIHandle, 4> resets;
        rhiCtx->GetView<ViewHistoryResetTag>().each([&](RHI::RHIHandle view) { resets.push_back(view); });
        for (RHI::RHIHandle view : resets)
        {
            if (auto* history = rhiCtx->TryGet<ViewHistory>(view))
            {
                history->m_valid = false;
            }
            rhiCtx->Remove<ViewHistoryResetTag>(view);
        }

        rhiCtx->GetView<View, ViewShaderBindings>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle entity, const View& view, const ViewShaderBindings& bindings)
        {
            ViewHistory current;
            current.m_worldToView = view.m_worldToView;
            current.m_viewToClip  = view.m_viewToClip;
            current.m_jitter      = view.m_jitter;
            current.m_valid       = true;

            auto* history = rhiCtx->TryGet<ViewHistory>(entity);
            const ViewHistory& previous = (history && history->m_valid) ? *history : current;

            WriteViewConstants(view, previous, time, bindings.m_bindings);

            if (history)
            {
                *history = current;
            }
        });
    }
}
