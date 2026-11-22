#include "DirectLightingPass.h"
#include "LightWeightTree.h"

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/Scene/SceneBinding.h>
#include <Binding/View/ViewBinding.h>
#include <CoreComponents/Tags.h>
#include <Feature/NRD/NRDViewOutput.h>
#include <Feature/SceneTextures/SceneTextures.h>
#include <Geometry/MeshGeometry.h>
#include <Log/ILogSystem.h>
#include <Pass/RenderPass.h>
#include <RayTracing/RayTracingScene.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <View/ViewTags.h>

namespace Spark::Render
{
    namespace
    {
        struct SceneTexture
        {
            const char* m_name;
            const char* m_input;
        };

        constexpr SceneTexture GBufferTextures[] = {
            { "GBufferNormal", "g_GBufferNormal" },
            { "GBufferSurface", "g_GBufferSurface" },
            { "GBufferBaseColor", "g_GBufferBaseColor" },
        };

        RenderPassConfig MakeConfig(const char* path)
        {
            auto* assets = Service<Resource::AssetManager>::Get();
            RenderPassConfig config;
            config.m_vertexShader = assets->LoadAsset<Resource::ShaderAsset>(
                Resource::AssetId::Of<Resource::ShaderAsset>(path));
            config.m_fragmentShader = config.m_vertexShader;
            RHI::InputStreamLayoutBuilder input;
            input.SetTopology(RHI::PrimitiveTopology::TriangleList);
            config.m_inputLayout = input.End();
            config.m_renderStates.m_depthStencilState.m_depth.m_enable = 0;
            config.m_renderStates.m_depthStencilState.m_stencil.m_enable = 0;
            config.m_renderStates.m_rasterState.m_cullMode = RHI::CullMode::None;
            return config;
        }
    }

    void DirectLightingPass::SetUp(PassContext& ctx)
    {
        namespace Signal = SceneTextures::DirectLighting;
        auto config = MakeConfig("engine://Shaders/Lighting/DirectLighting.hlsl");
        config.m_renderTargetLayout.m_colorAttachmentCount = 5;
        config.m_renderTargetLayout.m_colorFormats[0] = Signal::kRadianceFormat;
        config.m_renderTargetLayout.m_colorFormats[1] = Signal::kRadianceFormat;
        config.m_renderTargetLayout.m_colorFormats[2] = Signal::kNormalRoughnessFormat;
        config.m_renderTargetLayout.m_colorFormats[3] = Signal::kViewZFormat;
        config.m_renderTargetLayout.m_colorFormats[4] = Signal::kMotionFormat;

        SPARK_RENDER_PASS(ctx, "DirectLightingPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(config.m_vertexShader)
            .FragmentShader(config.m_fragmentShader)
            .InputLayout(config.m_inputLayout)
            .RenderTargetLayout(config.m_renderTargetLayout)
            .RenderStates(config.m_renderStates)
            .RendersView<MainViewTag>()
            .Build([](RenderPassScopes& p)
            {
                p.ImportShared<ViewBufferTag>(RHI::AttachmentId("Views"));
                // Import independently: a missing startup resource must not prevent
                // the remaining imports from being registered this frame.
                const bool hasLights = p.ImportShared<LightBufferTag>(RHI::AttachmentId("SceneLights"));
                const bool hasScene = p.ImportShared<RayTracingSceneTag>(RHI::AttachmentId("LightingScene"));
                const bool hasInstances = p.ImportShared<InstancePoolTag>(RHI::AttachmentId("LightingInstances"));
                const bool hasGeometry = p.ImportShared<GeometryPoolTag>(RHI::AttachmentId("LightingGeometry"));
                const bool hasMesh = p.ImportShared<MeshGeometryPoolTag>(RHI::AttachmentId("LightingMesh"));
                const bool hasMaterials = p.ImportShared<MaterialPoolTag>(RHI::AttachmentId("LightingMaterials"));
                const bool hasEmitters = p.ImportShared<EmissiveInstanceIDBufferTag>(RHI::AttachmentId("LightingEmitters"));
                p.ImportSharedSet<MaterialTextureTag>(RHI::AttachmentId("MaterialTextures"));
                const auto lightLayout = LightWeightTree::LayOut(*RHI::RHIExecuteContext::Current());
                const uint32_t emissiveCount = hasEmitters ? lightLayout.m_emissiveInstanceCount : 0;
                const bool ready = hasLights && hasScene && hasInstances && hasGeometry && hasMesh && hasMaterials;
                const auto size = p.GetRenderSize();
                auto scope = p.Scope();
                auto target = [&](const RHI::AttachmentId& name, RHI::Format format, const RHI::ClearValue& value)
                {
                    p.CreateImage(name, RHI::ImageDescriptor::Create2D(
                        RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead, size.x, size.y, format));
                    RHI::AttachmentLoadStoreAction clear;
                    clear.m_loadAction = RHI::AttachmentLoadAction::Clear;
                    clear.m_storeAction = RHI::AttachmentStoreAction::Store;
                    clear.m_clearValue = value;
                    scope.RenderTarget(name, clear);
                };
                const auto zero = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 0.f);
                target(Signal::DiffuseName(), Signal::kRadianceFormat, zero);
                target(Signal::SpecularName(), Signal::kRadianceFormat, zero);
                target(Signal::NormalRoughnessName(), Signal::kNormalRoughnessFormat,
                    RHI::ClearValue::CreateVector4Float(0.f, 0.f, 1.f, 1.f));
                target(Signal::ViewZName(), Signal::kViewZFormat,
                    RHI::ClearValue::CreateVector4Float(1e30f, 0.f, 0.f, 0.f));
                target(Signal::MotionName(), Signal::kMotionFormat, zero);

                scope.ReadBuffer(RHI::AttachmentId("Views"));
                for (const auto& texture : GBufferTextures)
                {
                    scope.ReadImage(RHI::AttachmentId(texture.m_name)).Bind(RHI::InputName(texture.m_input));
                }
                scope.ReadImage(RHI::AttachmentId("SceneDepth")).Format(RHI::Format::R32_FLOAT).Bind(RHI::InputName("g_Depth"));
                scope.ReadImage(RHI::AttachmentId("Velocity")).Bind(RHI::InputName("g_Velocity"));
                scope.ReadImage(RHI::AttachmentId("GBufferGeometricNormal")).Bind(RHI::InputName("g_GBufferGeometricNormal"));
                scope.ReadImage(RHI::AttachmentId("GBufferShadowOriginOffset")).Bind(RHI::InputName("g_GBufferShadowOriginOffset"));
                uint32_t lightWeightTreeLeafBase = 0;
                if (ready)
                {
                    // Direct shared imports derive their stages from the shader
                    // input layout. Do not narrow a potentially multi-stage input.
                    scope.ReadBuffer(RHI::AttachmentId("SceneLights"));
                    scope.ReadAccelerationStructure(RHI::AttachmentId("LightingScene"));
                    scope.ReadBuffer(RHI::AttachmentId("LightingInstances"));
                    scope.ReadBuffer(RHI::AttachmentId("LightingGeometry"));
                    scope.ReadBuffer(RHI::AttachmentId("LightingMesh"));
                    scope.ReadBuffer(RHI::AttachmentId("LightingMaterials"));
                    // Bindless textures have no directly bound input to infer from.
                    scope.ReadSet(RHI::AttachmentId("MaterialTextures")).Stage(RHI::AttachmentStage::FragmentShader);
                    if (hasEmitters)
                    {
                        scope.ReadBuffer(RHI::AttachmentId("LightingEmitters"));
                    }
                    if (lightLayout.m_leafBase != 0)
                    {
                        lightWeightTreeLeafBase = lightLayout.m_leafBase;
                        scope.ReadBuffer(RHI::AttachmentId(LightWeightTree::BufferName))
                            .View(RHI::BufferViewDescriptor::CreateStructured(0, lightLayout.NodeCount(), sizeof(float)))
                            .Bind(RHI::InputName("g_LightWeightTree"));
                    }
                }
                scope.Constant(RHI::InputName("emissiveInstanceCount"), emissiveCount);
                scope.Constant(RHI::InputName("lightingReady"), ready ? 1u : 0u);
                scope.Constant(RHI::InputName("lightWeightTreeLeafBase"), lightWeightTreeLeafBase);
                scope.Sampler(RHI::InputName("g_MaterialSampler"), RHI::SamplerState::Create(
                    RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));
                scope.Draw(RHI::DrawLinear(3, 0));
                scope.Close();
            })
            .Finalize();
    }

    void DirectLightingPass::SetUpComposite(PassContext& ctx)
    {
        namespace Signal = SceneTextures::DirectLighting;
        auto config = MakeConfig("engine://Shaders/Lighting/DirectLightingComposite.hlsl");
#if SPARK_WITH_NRD
        auto* assets = Service<Resource::AssetManager>::Get();
        const auto nrdShader = assets->LoadAsset<Resource::ShaderAsset>(
            Resource::AssetId::Of<Resource::ShaderAsset>("engine://Shaders/Lighting/DirectLightingNRDComposite.hlsl"));
        const bool useNRDShader = nrdShader && nrdShader->GetStageReflection(RHI::ShaderStage::Vertex)
            && nrdShader->GetStageReflection(RHI::ShaderStage::Fragment);
        if (useNRDShader)
        {
            config.m_vertexShader = nrdShader;
            config.m_fragmentShader = nrdShader;
        }
        else
        {
            LOG_WARN("[DirectLightingPass] Could not load the NRD composite shader. Using raw direct lighting.");
        }
#else
        constexpr bool useNRDShader = false;
#endif

        config.m_renderTargetLayout.m_colorAttachmentCount = 1;
        config.m_renderTargetLayout.m_colorFormats[0] = RHI::Format::R16G16B16A16_FLOAT;
        auto& blend = config.m_renderStates.m_blendState.m_targets[0];
        blend.m_enable = 1;
        blend.m_blendSource = RHI::BlendFactor::One;
        blend.m_blendDest = RHI::BlendFactor::One;
        blend.m_blendOp = RHI::BlendOp::Add;
        blend.m_blendAlphaSource = RHI::BlendFactor::Zero;
        blend.m_blendAlphaDest = RHI::BlendFactor::One;
        blend.m_blendAlphaOp = RHI::BlendOp::Add;

        SPARK_RENDER_PASS(ctx, "DirectLightingCompositePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(config.m_vertexShader)
            .FragmentShader(config.m_fragmentShader)
            .InputLayout(config.m_inputLayout)
            .RenderTargetLayout(config.m_renderTargetLayout)
            .RenderStates(config.m_renderStates)
            .RendersView<MainViewTag>()
            .Build([useNRDShader](RenderPassScopes& p)
            {
                p.ImportShared<ViewBufferTag>(RHI::AttachmentId("Views"));
                RHI::AttachmentLoadStoreAction load;
                load.m_loadAction = RHI::AttachmentLoadAction::Load;
                load.m_storeAction = RHI::AttachmentStoreAction::Store;
                auto scope = p.Scope();
                scope.ReadBuffer(RHI::AttachmentId("Views"));
                scope.ReadImage(Signal::DiffuseName()).Bind(RHI::InputName("g_DirectDiffuse"));
                scope.ReadImage(Signal::SpecularName()).Bind(RHI::InputName("g_DirectSpecular"));
                if (useNRDShader)
                {
                    auto& rhiContext = *RHI::RHIExecuteContext::Current();
                    const auto view = rhiContext.GetView<MainViewTag, NRDViewOutput>(Exclude<DeadTag, ViewInactiveTag>).front();
                    const auto* output = view == RHI::NullHandle ? nullptr : &rhiContext.Get<NRDViewOutput>(view);

                    // Raw views fill the optional slots on frames with an empty NRD rect.
                    scope.ReadImage(output ? NRDOutputs::Diffuse : Signal::DiffuseName()).Bind(RHI::InputName("g_NRDDiffuse"));
                    scope.ReadImage(output ? NRDOutputs::Specular : Signal::SpecularName()).Bind(RHI::InputName("g_NRDSpecular"));
                    scope.ReadImage(Signal::ViewZName()).Bind(RHI::InputName("g_LightingViewZ"));
                    scope.Constant(RHI::InputName("nrdRectSize"), output ? output->m_rectSize : eastl::array<uint32_t, 2>{});
                    scope.Constant(RHI::InputName("nrdDenoisingRange"), output ? output->m_denoisingRange : 0.0f);
                }
                // Only material colors survive reflection in the composition shader.
                scope.ReadImage(RHI::AttachmentId("GBufferSurface")).Bind(RHI::InputName("g_GBufferSurface"));
                scope.ReadImage(RHI::AttachmentId("GBufferBaseColor")).Bind(RHI::InputName("g_GBufferBaseColor"));
                scope.RenderTarget(RHI::AttachmentId("SceneColor"), load);
                scope.Draw(RHI::DrawLinear(3, 0));
                scope.Close();
            })
            .Finalize();
    }
}
