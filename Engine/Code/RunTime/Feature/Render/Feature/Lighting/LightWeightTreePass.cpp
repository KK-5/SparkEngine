#include "LightWeightTreePass.h"
#include "LightWeightTree.h"

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/Scene/SceneBinding.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>
#include <Math/MathUtils.h>
#include <Pass/ComputePass.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

namespace Spark::Render
{
    LightWeightTree::Layout LightWeightTree::LayOut(RHI::RHIContext& context)
    {
        Layout layout;
        for (auto [entity, count] : context.GetView<SceneLightCount>().each())
        {
            layout.m_sceneLightCount = count.m_count;
        }
        for (auto [entity, count] : context.GetView<EmissiveInstanceCount>().each())
        {
            layout.m_emissiveInstanceCount = count.m_count;
        }
        const uint32_t lightCount = layout.LightCount();
        layout.m_leafBase = lightCount != 0 ? NextPowerOfTwo(lightCount) : 0;
        return layout;
    }

    void LightWeightTreePass::SetUp(PassContext& ctx)
    {
        auto* assets = Service<Resource::AssetManager>::Get();
        ASSERT(assets, "AssetManager is unregister.");
        const auto assetId = assets->MakeAssetId("engine://Shaders/Lighting/LightWeightTree.hlsl");
        const auto shader = assetId.IsValid()
            ? assets->LoadAsset<Resource::ShaderAsset>(assetId) : Ptr<Resource::ShaderAsset>{};
        if (!shader)
        {
            LOG_ERROR("[LightWeightTreePass] Failed to load shader LightWeightTree.hlsl.");
            return;
        }

        SPARK_COMPUTE_PASS(ctx, "LightWeightTreePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shader)
            .Build([](ComputePassScopes& p)
            {
                auto& rhiContext = *RHI::RHIExecuteContext::Current();
                const auto layout = LightWeightTree::LayOut(rhiContext);
                if (layout.m_leafBase == 0)
                {
                    return;
                }
                const RHI::AttachmentId lightsName("SceneLights");
                const RHI::AttachmentId emittersName("LightingEmitters");
                const RHI::AttachmentId instancesName("LightingInstances");
                const RHI::AttachmentId geometryName("LightingGeometry");
                const RHI::AttachmentId materialsName("LightingMaterials");
                if (layout.m_sceneLightCount != 0 && !p.ImportShared<LightBufferTag>(lightsName))
                {
                    return;
                }
                if (layout.m_emissiveInstanceCount != 0 && !(p.ImportShared<EmissiveInstanceIDBufferTag>(emittersName)
                    && p.ImportShared<InstancePoolTag>(instancesName)
                    && p.ImportShared<GeometryPoolTag>(geometryName)
                    && p.ImportShared<MaterialPoolTag>(materialsName)))
                {
                    return;
                }

                float sceneRadiusSquared = 0.0f;
                for (auto [entity, bounds] : rhiContext.GetView<OpaqueSceneBounds>().each())
                {
                    if (bounds.m_valid)
                    {
                        const Math::Vector3 extents = bounds.m_worldBounds.Extents();
                        sceneRadiusSquared = Math::Dot(extents, extents);
                    }
                }
                const uint32_t nodeCount = layout.NodeCount();
                const RHI::AttachmentId treeName(LightWeightTree::BufferName);
                RHI::BufferDescriptor desc;
                desc.m_byteCount = static_cast<uint64_t>(nodeCount) * sizeof(float);
                desc.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite;
                p.CreateBuffer(treeName, desc);
                const auto treeView = RHI::BufferViewDescriptor::CreateStructured(0, nodeCount, sizeof(float));
                const RHI::InputName treeInput("g_WeightTree");

                for (uint32_t nodeBase = layout.m_leafBase; nodeBase != 0; nodeBase >>= 1)
                {
                    const bool writeLeaves = nodeBase == layout.m_leafBase;
                    auto scope = p.Scope();
                    if (writeLeaves && layout.m_sceneLightCount != 0)
                    {
                        scope.ReadBuffer(lightsName);
                    }
                    if (writeLeaves && layout.m_emissiveInstanceCount != 0)
                    {
                        scope.ReadBuffer(emittersName);
                        scope.ReadBuffer(instancesName);
                        scope.ReadBuffer(geometryName);
                        scope.ReadBuffer(materialsName);
                    }
                    // One scope per level gives the graph a dependency between UAV accesses.
                    auto tree = writeLeaves ? scope.WriteBuffer(treeName) : scope.ReadWriteBuffer(treeName);
                    tree.View(treeView).Bind(treeInput);
                    scope.Constant(RHI::InputName("nodeBase"), nodeBase);
                    scope.Constant(RHI::InputName("leafBase"), layout.m_leafBase);
                    scope.Constant(RHI::InputName("sceneLightCount"), layout.m_sceneLightCount);
                    scope.Constant(RHI::InputName("lightCount"), layout.LightCount());
                    scope.Constant(RHI::InputName("sceneRadiusSquared"), sceneRadiusSquared);
                    scope.Dispatch(nodeBase);
                    scope.Close();
                }
            })
            .Finalize();
    }
}
