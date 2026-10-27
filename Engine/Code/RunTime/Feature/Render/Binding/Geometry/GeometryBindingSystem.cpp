#include "GeometryBindingSystem.h"

#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <ECS/Common.h>
#include <Service/Service.h>

#include <RHI/HardwareQueue.h>
#include <RHI/RHIInterface.h>
#include <RHI/Device/Device.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Pass/ComputePass.h>
#include <RenderGraph/PassScopes.h>

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
        // The layout of GeometryBindings.hlsli, declared rather than reflected (see SharedBindings.h).
        RHI::ShaderInputList inputs;
        inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
            RHI::InputName(GeometryBufferName), RHI::ShaderInputBufferAccess::Read,
            RHI::ShaderInputBufferType::Structured, 1, 0, 0, kGeometrySpaceId));

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The DeviceArray binds the g_Geometries SRV.
        Ptr<RHI::ShaderBindings> geometryBindings =
            CreateSharedBindings(inputs, kGeometrySpaceId, "GeometryBindingSystem");
        if (!geometryBindings)
        {
            return;
        }

        m_bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindingsEntity, RHI::Components::ShaderBindings{ geometryBindings });
        rhiCtx.Add<GeometryBindingTag>(m_bindingsEntity);

        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        DeviceArray::Descriptor arrayDesc;
        arrayDesc.m_name        = ObjectName(GeometryBufferName);
        arrayDesc.m_elementSize = sizeof(GeometryData);
        arrayDesc.m_capacity    = Capacity;
        arrayDesc.m_inputName   = RHI::InputName(GeometryBufferName);
        arrayDesc.m_bindings    = m_bindingsEntity;
        if (m_geometries.Init(rhiCtx, device, arrayDesc))
        {
            rhiCtx.Add<GeometryPoolTag>(m_geometries.GetResource());
        }
    }

    void GeometryBindingSystem::SetUp(PassContext& passContext)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/ElementUpdate/ElementUpdate.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[GeometryBindingSystem] Failed to load shader ElementUpdate.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(passContext, "GeometryUpdate")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([this](ComputePassScopes& p)
            {
                const ElementUpdateBuffer& updates     = m_geometries.GetUpdates();
                const uint32_t             updateCount = updates.GetUpdateCount();
                if (updateCount == 0)
                {
                    return;
                }

                const RHI::AttachmentId updatesName("GeometryUpdate");
                const RHI::AttachmentId geometriesName("Geometries");
                p.Import(updatesName, updates.GetResource());
                p.Import(geometriesName, m_geometries.GetResource());

                const uint32_t elementWords = updates.GetElementSize() / sizeof(uint32_t);

                auto s = p.Scope();
                s.ReadBuffer(updatesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(updates.GetByteCount())))
                    .Bind(RHI::InputName("g_Updates"));
                // Read too: the records not updated stay.
                s.ReadWriteBuffer(geometriesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(m_geometries.GetByteCount())))
                    .Bind(RHI::InputName("g_Elements"));
                s.Constant(RHI::InputName("updateCount"), updateCount);
                s.Constant(RHI::InputName("elementWords"), elementWords);
                s.Constant(RHI::InputName("indicesOffset"), static_cast<uint32_t>(updates.GetIndicesOffset()));
                s.Constant(RHI::InputName("elementsOffset"), static_cast<uint32_t>(updates.GetElementsOffset()));
                s.Dispatch(updateCount * elementWords);
                s.Close();
            })
            .Finalize();
    }

    void GeometryBindingSystem::Update()
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        AllocateSlots(*world);
        m_geometries.BeginUpdate();
        world->GetView<GeometrySlot, MeshGeometry>().each(
            [&](Entity, const GeometrySlot& slot, const MeshGeometry& geometry)
        {
            m_geometries.Set(slot.Get(), EncodeGeometryData(geometry));
        });
        m_geometries.EndUpdate();
    }

    void GeometryBindingSystem::AllocateSlots(WorldContext& world)
    {
        // Collected first: GeometrySlot is the pool being iterated.
        eastl::vector<Entity> lostGeometry;
        world.GetView<GeometrySlot>(Exclude<MeshGeometry>).each([&](Entity entity, const GeometrySlot&)
        {
            lostGeometry.push_back(entity);
        });
        for (Entity entity : lostGeometry)
        {
            world.Remove<GeometrySlot>(entity);
        }

        // Structural write inside iteration: GeometrySlot is only in the exclude set.
        world.GetView<MeshGeometry>(Exclude<GeometrySlot, DeadTag>).each(
            [&](Entity entity, const MeshGeometry&)
        {
            Suballocation range = m_geometries.Allocate();
            if (!range.IsValid())
            {
                LOG_ERROR("[GeometryBindingSystem] All {} geometry slots are taken; a mesh is not drawn.", Capacity);
                return;
            }
            world.Add<GeometrySlot>(entity, GeometrySlot{ eastl::move(range) });
        });
    }

    void GeometryBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<GeometrySlot>();
        }
        m_geometries.Shutdown(rhiCtx);

        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }

        m_bindingsEntity = RHI::NullHandle;
    }
}
