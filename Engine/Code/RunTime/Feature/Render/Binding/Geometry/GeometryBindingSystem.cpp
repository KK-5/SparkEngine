#include "GeometryBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <ECS/Common.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

namespace Spark::Render
{
    namespace
    {
        constexpr const char* GeometryBufferName = "g_Geometries";
    }

    GeometryData EncodeGeometryData(const MeshGeometry& geometry)
    {
        GeometryData data;
        data.m_firstIndex    = geometry.m_firstIndex;
        data.m_indexCount    = geometry.m_indexCount;
        data.m_vertexOffset  = static_cast<int32_t>(geometry.m_vertexOffset);
        data.m_boundsCenter  = geometry.m_localBounds.Center();
        data.m_boundsExtents = geometry.m_localBounds.Extents();
        return data;
    }

    void GeometryBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[GeometryBindingSystem] AssetManager is unregistered.");

        // GeometryBindingsReflect.hlsl is a reflection host (#includes GeometryBindings.hlsli
        // + a dummy vertex entry reading g_Geometries) so we can reflect the space6 layout
        // here — mirrors InstanceBindingSystem / InstanceBindingsReflect.hlsl.
        const Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/GeometryBindingsReflect.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[GeometryBindingSystem] Failed to resolve GeometryBindingsReflect.hlsl asset id.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        if (!shaderAsset)
        {
            LOG_ERROR("[GeometryBindingSystem] Failed to load GeometryBindingsReflect.hlsl.");
            return;
        }

        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[GeometryBindingSystem] GeometryBindings.hlsli produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[GeometryBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[GeometryBindingSystem] RHI factory or device is null.");

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Geometries SRV is bound by GlobalBuffer once the upload
        // buffer materializes.
        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> geometryBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 6;   // GeometryBindings (per-geometry) is reserved at space6.
        if (geometryBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[GeometryBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        m_bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindingsEntity, RHI::Components::ShaderBindings{ geometryBindings });
        rhiCtx.Add<GeometryBindingTag>(m_bindingsEntity);

        GlobalBuffer<Geometries, GeometryData, MeshGeometry>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(GeometryBufferName);
        bufferDesc.m_inputName      = RHI::InputName(GeometryBufferName);
        bufferDesc.m_bindingsEntity = m_bindingsEntity;
        m_geometries.Init(rhiCtx, bufferDesc);
    }

    void GeometryBindingSystem::Update(uint32_t frameIndex)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        m_geometries.Update(*world, *rhiCtx, frameIndex,
            [](Entity, GeometryData& out, const MeshGeometry& geometry)
        {
            out = EncodeGeometryData(geometry);
        });
    }

    void GeometryBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            m_geometries.Shutdown(*world, rhiCtx);
        }

        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }

        m_bindingsEntity = RHI::NullHandle;
    }
}
