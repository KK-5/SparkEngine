#include "ViewBindingSystem.h"

#include <cmath>
#include <string.h>

#include <EASTL/algorithm.h>
#include <EASTL/fixed_vector.h>

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <Math/Vector4.h>
#include <Service/Service.h>
#include <RHI/RHIInterface.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Binding/SharedGroupLayouts.h>
#include <Binding/ShaderUsage.h>
#include <View/ViewComponents.h>
#include <View/ViewTags.h>

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

        ViewData EncodeViewData(const View& view, const ViewHistory& previous, const FrameTime& time)
        {
            ViewData data;

            const Math::Matrix4X4 viewProjection     = view.GetJitteredWorldToClip();
            const Math::Matrix4X4 viewProjectionNoAA = view.GetWorldToClip();
            const Math::Matrix4X4 prevViewProjection = previous.m_viewToClip * previous.m_worldToView;

            const float bufferWidth  = static_cast<float>(view.m_bufferSize.x);
            const float bufferHeight = static_cast<float>(view.m_bufferSize.y);
            const float viewWidth    = bufferWidth  * (view.m_rect.m_maxX - view.m_rect.m_minX);
            const float viewHeight   = bufferHeight * (view.m_rect.m_maxY - view.m_rect.m_minY);

            data.m_viewProjection     = viewProjection;
            data.m_invViewProj        = Math::Inverse(viewProjection);
            data.m_view               = view.m_worldToView;
            data.m_invView            = Math::Inverse(view.m_worldToView);
            data.m_viewProjectionNoAA = viewProjectionNoAA;
            data.m_prevViewProjection = prevViewProjection;
            data.m_prevView           = previous.m_worldToView;
            data.m_clipToPrevClip     = prevViewProjection * Math::Inverse(viewProjectionNoAA);

            data.m_temporalAAJitter =
                Math::Vector4(view.m_jitter.x, view.m_jitter.y, previous.m_jitter.x, previous.m_jitter.y);
            // Rounded: a fraction times the buffer size can land a hair off a whole pixel.
            data.m_viewRectMin = Math::Vector4(
                std::round(bufferWidth * view.m_rect.m_minX), std::round(bufferHeight * view.m_rect.m_minY), 0.0f, 0.0f);
            const bool ownInput = view.m_inputBufferSize == Math::Vector2Int(0, 0);
            const ViewRect& inputRect = ownInput ? view.m_rect : view.m_inputRect;
            const float inputWidth  = ownInput ? bufferWidth  : static_cast<float>(view.m_inputBufferSize.x);
            const float inputHeight = ownInput ? bufferHeight : static_cast<float>(view.m_inputBufferSize.y);
            data.m_inputViewRectMin = Math::Vector4(
                std::round(inputWidth * inputRect.m_minX), std::round(inputHeight * inputRect.m_minY), 0.0f, 0.0f);
            data.m_viewSizeAndInvSize        = SizeAndInvSize(viewWidth, viewHeight);
            data.m_bufferSizeAndInvSize      = SizeAndInvSize(bufferWidth, bufferHeight);
            data.m_inputBufferSizeAndInvSize = SizeAndInvSize(inputWidth, inputHeight);
            data.m_invDeviceZToViewZ         = Math::DeviceZToViewZParams(view.m_viewToClip);
            data.m_prevInvDeviceZToViewZ     = Math::DeviceZToViewZParams(previous.m_viewToClip);

            data.m_exposure = view.m_exposure;

            // P3's EyeAdaptation writes this from last frame's measured exposure; until
            // then it is 1, so the multiply and divide cancel exactly.
            constexpr float preExposure = 1.0f;
            data.m_preExposure        = preExposure;
            data.m_oneOverPreExposure = 1.0f / preExposure;

            data.m_frameNumber  = static_cast<uint32_t>(time.m_frameNumber);
            data.m_gameTime     = static_cast<float>(time.m_gameTime);
            data.m_prevGameTime = static_cast<float>(time.m_prevGameTime);
            data.m_deltaTime    = time.m_deltaTime;
            data.m_temporalHistoryValid = previous.m_valid ? 1u : 0u;
            return data;
        }

        ViewHistory CurrentHistory(const View& view, const FrameTime& time)
        {
            ViewHistory current;
            current.m_worldToView = view.m_worldToView;
            current.m_viewToClip  = view.m_viewToClip;
            current.m_jitter      = view.m_jitter;
            current.m_bufferSize  = view.m_bufferSize;
            current.m_rect        = view.m_rect;
            current.m_frameNumber = time.m_frameNumber;
            current.m_valid       = true;
            return current;
        }

        //! Fixed-size history only. Projection/rect changes restart accumulation;
        //! jitter is intentionally excluded from the compatibility test.
        bool IsHistoryCompatible(const ViewHistory& previous, const View& view, const FrameTime& time)
        {
            return previous.m_valid
                && time.m_frameNumber > previous.m_frameNumber
                && time.m_frameNumber - previous.m_frameNumber == 1u
                && previous.m_bufferSize == view.m_bufferSize
                && previous.m_viewToClip == view.m_viewToClip
                && previous.m_rect.m_minX == view.m_rect.m_minX
                && previous.m_rect.m_maxX == view.m_rect.m_maxX
                && previous.m_rect.m_minY == view.m_rect.m_minY
                && previous.m_rect.m_maxY == view.m_rect.m_maxY;
        }

        //! Fallback matrices keep motion finite, but must not make history valid.
        ViewHistory PreviousHistory(RHI::RHIContext& rhiCtx, RHI::RHIHandle entity,
                                    const View& view, const FrameTime& time)
        {
            const auto* history = rhiCtx.TryGet<ViewHistory>(entity);
            if (history && IsHistoryCompatible(*history, view, time))
            {
                return *history;
            }
            ViewHistory fallback = CurrentHistory(view, time);
            fallback.m_valid = false;
            return fallback;
        }
    }

    void ViewBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {



        StagingBuffer::Descriptor bufferDesc;
        bufferDesc.m_name      = ObjectName(SharedInput::Views);
        bufferDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead;
        m_viewBuffer.Init(*Service<RHI::RHIInterface>::Get()->GetDevice(), bufferDesc);
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

        // Last frame's rows are gone, so are the indices: a view not packed below keeps none.
        rhiCtx->Clear<ViewIndex>();
        m_views.clear();
        // Structural write inside iteration: ViewIndex is not an iterated pool.
        rhiCtx->GetView<View>(Exclude<DeadTag>).each([&](RHI::RHIHandle entity, const View& view)
        {
            rhiCtx->Add<ViewIndex>(entity, ViewIndex{ static_cast<uint32_t>(m_views.size()) });
            m_views.push_back(EncodeViewData(view, PreviousHistory(*rhiCtx, entity, view, time), time));
        });
        const bool written = WriteAndBind(*rhiCtx);
        if (!written)
        {
            rhiCtx->Clear<ViewIndex>();
        }

        rhiCtx->GetView<View, ViewHistory>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle entity, const View& view, ViewHistory& history)
        {
            history = CurrentHistory(view, time);
            history.m_valid = written && !rhiCtx->Has<ViewInactiveTag>(entity);
        });
    }

    void ViewBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.Clear<ViewIndex>();

        m_viewBuffer.Shutdown();
        m_bufferResource = RHI::NullHandle;
        m_views.clear();
    }

    bool ViewBindingSystem::WriteAndBind(RHI::RHIContext& rhiCtx)
    {
        // Room for one row at least, so the view is never empty.
        const uint64_t byteCount = eastl::max<uint64_t>(m_views.size(), 1) * sizeof(ViewData);
        m_viewBuffer.Begin(byteCount);
        const StagingBuffer::Allocation allocation = m_viewBuffer.Allocate(byteCount);
        RHI::Buffer* buffer = m_viewBuffer.GetBuffer();
        if (!allocation.m_data || !buffer)
        {
            return false;
        }
        if (!m_views.empty())
        {
            memcpy(allocation.m_data, m_views.data(), m_views.size() * sizeof(ViewData));
        }

        // A view of the whole buffer, so each buffer gets one whatever the count. Published
        // rather than bound: it changes with the buffer, so the usage is refreshed not set once.
        const RHI::BufferViewDescriptor viewDesc = RHI::BufferViewDescriptor::CreateStructured(
            0, static_cast<uint32_t>(buffer->GetDescriptor().m_byteCount / sizeof(ViewData)), sizeof(ViewData));

        // The slots rotate, so the tag follows this frame's: a pass imports the buffer by it.
        const RHI::RHIHandle resource = m_viewBuffer.GetResource();
        if (m_bufferResource != resource)
        {
            if (m_bufferResource != RHI::NullHandle && rhiCtx.Valid(m_bufferResource))
            {
                rhiCtx.Remove<ViewBufferTag>(m_bufferResource);
            }
            rhiCtx.Add<ViewBufferTag>(resource);
            m_bufferResource = resource;
        }

        rhiCtx.AddOrReplace<ShaderBufferUsage>(resource, ShaderBufferUsage{
            ShaderInputBinding{ kViewSpaceId, RHI::InputName(SharedInput::Views), 0 }, viewDesc });
        return true;
    }
}
