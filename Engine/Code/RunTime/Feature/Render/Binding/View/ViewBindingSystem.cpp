#include "ViewBindingSystem.h"

#include <cmath>

#include <EASTL/fixed_vector.h>

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <Math/Vector4.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <View/ViewComponents.h>

namespace Spark::Render
{
    namespace
    {
        constexpr const char* ViewBufferName = "g_Views";

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
            return data;
        }

        ViewHistory CurrentHistory(const View& view)
        {
            ViewHistory current;
            current.m_worldToView = view.m_worldToView;
            current.m_viewToClip  = view.m_viewToClip;
            current.m_jitter      = view.m_jitter;
            current.m_valid       = true;
            return current;
        }

        //! A view without a valid history encodes its previous frame as the current one.
        ViewHistory PreviousHistory(RHI::RHIContext& rhiCtx, RHI::RHIHandle entity, const View& view)
        {
            const auto* history = rhiCtx.TryGet<ViewHistory>(entity);
            return (history && history->m_valid) ? *history : CurrentHistory(view);
        }
    }

    void ViewBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[ViewBindingSystem] AssetManager is unregistered.");

        // ViewBindingsReflect.hlsl is a reflection host (includes ViewBindings.hlsli + a dummy
        // vertex entry reading g_Views) so the space1 layout can be reflected here — mirrors
        // MaterialBindingSystem / MaterialBindingsReflect.hlsl.
        const Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/ViewBindingsReflect.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[ViewBindingSystem] Failed to resolve ViewBindingsReflect.hlsl asset id.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        if (!shaderAsset)
        {
            LOG_ERROR("[ViewBindingSystem] Failed to load ViewBindingsReflect.hlsl.");
            return;
        }

        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[ViewBindingSystem] ViewBindings.hlsli produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[ViewBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[ViewBindingSystem] RHI factory or device is null.");

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Views SRV is bound by GlobalBuffer once the upload
        // buffer materializes, so we DO NOT mark it dirty here.
        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> viewBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 1;   // The shared view group is reserved at space1.
        if (viewBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ViewBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        m_bindings = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindings, RHI::Components::ShaderBindings{ viewBindings });
        rhiCtx.Add<ViewBindingTag>(m_bindings);

        GlobalBuffer<Views, ViewData, View>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(ViewBufferName);
        bufferDesc.m_inputName      = RHI::InputName(ViewBufferName);
        bufferDesc.m_bindingsEntity = m_bindings;
        m_views.Init(rhiCtx, bufferDesc);
    }

    void ViewBindingSystem::Update(uint32_t frameIndex, const FrameTime& time)
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

        m_views.Update(*rhiCtx, *rhiCtx, frameIndex,
            [&](RHI::RHIHandle entity, ViewData& row, const View& view)
        {
            row = EncodeViewData(view, PreviousHistory(*rhiCtx, entity, view), time);
        });
        // Apart from the encode, which does not run at all before the buffer materializes.
        rhiCtx->GetView<View, ViewHistory>(Exclude<DeadTag>).each(
            [&](const View& view, ViewHistory& history)
        {
            history = CurrentHistory(view);
        });
    }

    void ViewBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        m_views.Shutdown(rhiCtx, rhiCtx);

        if (m_bindings != RHI::NullHandle)
        {
            rhiCtx.Add<DeadTag>(m_bindings);
        }
        m_bindings = RHI::NullHandle;
    }
}
