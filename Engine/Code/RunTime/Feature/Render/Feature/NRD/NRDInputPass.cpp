#include "NRDInputPass.h"

#include <EASTL/array.h>
#include <Feature/SceneTextures/SceneTextures.h>
#include <Log/ILogSystem.h>
#include <Pass/ComputePass.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>

namespace Spark::Render
{
    Ptr<Resource::ShaderAsset> NRDInputPass::LoadShader()
    {
        auto* assets = Service<Resource::AssetManager>::Get();
        ASSERT(assets, "AssetManager is unregister.");
        const auto assetId = assets->MakeAssetId("engine://Shaders/Lighting/NRDInput.hlsl");
        auto shader = assetId.IsValid()
            ? assets->LoadAsset<Resource::ShaderAsset>(assetId) : Ptr<Resource::ShaderAsset>{};
        if (!shader)
        {
            LOG_ERROR("[NRDInputPass] Failed to load shader NRDInput.hlsl.");
        }
        return shader;
    }

    void NRDInputPass::SetUp(PassContext& ctx, const Ptr<Resource::ShaderAsset>& shader,
        RHI::Format normalRoughnessFormat)
    {
        namespace Signal = SceneTextures::DirectLighting;
        SPARK_COMPUTE_PASS(ctx, "NRDInputPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shader)
            .Build([normalRoughnessFormat](ComputePassScopes& pass)
            {
                const auto size = pass.GetRenderSize();
                if (size.x <= 0 || size.y <= 0)
                {
                    return;
                }

                const auto width = static_cast<uint32_t>(size.x);
                const auto height = static_cast<uint32_t>(size.y);
                const auto image = [width, height](RHI::Format format)
                {
                    auto desc = RHI::ImageDescriptor::Create2D(RHI::ImageBindFlags::ShaderReadWrite, width, height, format);
                    desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
                    return desc;
                };
                constexpr auto packedFloatFormat = RHI::Format::R16G16B16A16_FLOAT;
                pass.CreateImage(NRDInputs::Diffuse, image(packedFloatFormat));
                pass.CreateImage(NRDInputs::Specular, image(packedFloatFormat));
                pass.CreateImage(NRDInputs::NormalRoughness, image(normalRoughnessFormat));
                pass.CreateImage(NRDInputs::Motion, image(packedFloatFormat));
                auto confidence = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::ShaderReadWrite, 1, 1, RHI::Format::R32_FLOAT);
                confidence.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
                pass.CreateImage(NRDInputs::HistoryConfidence, confidence);

                auto scope = pass.Scope();
                scope.ReadImage(Signal::DiffuseName()).Bind(RHI::InputName("g_RawDiffuse"));
                scope.ReadImage(Signal::SpecularName()).Bind(RHI::InputName("g_RawSpecular"));
                scope.ReadImage(Signal::NormalRoughnessName()).Bind(RHI::InputName("g_RawNormalRoughness"));
                scope.ReadImage(Signal::MotionName()).Bind(RHI::InputName("g_RawMotion"));
                scope.WriteImage(NRDInputs::Diffuse).Bind(RHI::InputName("g_Diffuse"));
                scope.WriteImage(NRDInputs::Specular).Bind(RHI::InputName("g_Specular"));
                scope.WriteImage(NRDInputs::NormalRoughness).Bind(RHI::InputName("g_NormalRoughness"));
                scope.WriteImage(NRDInputs::Motion).Bind(RHI::InputName("g_Motion"));
                scope.WriteImage(NRDInputs::HistoryConfidence).Bind(RHI::InputName("g_HistoryConfidence"));
                scope.Constant(RHI::InputName("inputSize"), eastl::array<uint32_t, 2>{ width, height });
                scope.Dispatch(width, height);
                scope.Close();
            })
            .Finalize();
    }
}
