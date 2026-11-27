#include "InstanceBindingSystem.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <ECS/Common.h>
#include <Service/Service.h>

#include <RHI/HardwareQueue.h>
#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Binding/SharedGroupLayouts.h>
#include <Pass/ComputePass.h>
#include <Pass/Component/RHIComponents.h>
#include <RenderGraph/PassScopes.h>

#include <Math/MathUtils.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/Scene/SceneBinding.h>
#include <Drawable/DrawMask.h>
#include <DrawList/DrawList.h>
#include <PipelineState/PipelineKeyIndex.h>

namespace Spark::Render
{
    namespace
    {

        //! The material's stable g_Materials slot. Allocated once by MaterialBindingSystem and
        //! never moved, so this reads whatever is there rather than depending on a value
        //! written this same frame; a material whose slot does not exist yet reads 0 for a frame.
        uint32_t MaterialIndexOf(const Material::MaterialContext& matCtx, Material::MaterialHandle mh)
        {
            if (const auto* slot = matCtx.TryGet<MaterialSlot>(mh))
            {
                return slot->Get();
            }
            return 0;
        }

        Resource::MaterialState MaterialStateOf(const Material::MaterialContext& matCtx, Material::MaterialHandle mh)
        {
            const auto* state = matCtx.TryGet<Resource::MaterialState>(mh);
            return state ? *state : Resource::MaterialState{};
        }

        // The active list is the InstanceSlot storage in its own order. entt keeps a storage
        // packed, adding at the end and moving the last into a hole, so a frame moves few
        // renderables; nothing depends on that but how many positions are written.

        //! A renderable's position in the active list.
        template<typename Storage>
        uint32_t PositionOf(const Storage& slots, Entity entity)
        {
            return static_cast<uint32_t>(slots.index(entity));
        }

        //! The slot of the renderable at a position of the active list.
        template<typename Storage>
        uint32_t SlotAt(const Storage& slots, uint32_t position)
        {
            return slots.get(slots[position]).Get();
        }

        void BuildInstanceIDUpdate(ComputePassScopes& p, const ActiveInstanceList& list,
            const RHI::AttachmentId& updatesName, const RHI::AttachmentId& idsName)
        {
            const ElementUpdateBuffer& updates = list.GetUpdates();
            const uint32_t updateCount = updates.GetUpdateCount();
            if (updateCount == 0)
            {
                return;
            }

            p.Import(updatesName, updates.GetResource());
            p.Import(idsName, list.GetResource());
            auto s = p.Scope();
            s.ReadBuffer(updatesName)
                .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(updates.GetByteCount())))
                .Bind(RHI::InputName("g_Updates"));
            // Positions not updated retain their contents. The count bounds all readers.
            s.ReadWriteBuffer(idsName)
                .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(ActiveInstanceList::ByteCount)))
                .Bind(RHI::InputName("g_Elements"));
            s.Constant(RHI::InputName("updateCount"), updateCount);
            s.Constant(RHI::InputName("elementWords"), 1u);
            s.Constant(RHI::InputName("indicesOffset"), static_cast<uint32_t>(updates.GetIndicesOffset()));
            s.Constant(RHI::InputName("elementsOffset"), static_cast<uint32_t>(updates.GetElementsOffset()));
            s.Dispatch(updateCount);
            s.Close();
        }
    }

    EncodedInstance EncodeInstanceData(WorldContext& world, Entity entity)
    {
        const auto& transform = world.Get<Transform::WorldTransformMatrix>(entity);
        EncodedInstance encoded;
        InstanceData& out = encoded.m_data;
        const auto* history = world.TryGet<InstanceHistory>(entity);
        const bool historyValid = history && history->m_valid && !world.Has<InstanceHistoryResetTag>(entity);
        out.m_model = transform.m_worldMatrix;
        out.m_prevModel = historyValid ? history->m_model : transform.m_worldMatrix;
        out.m_normalMatrix = Math::ToMatrix4X4(
            Math::Transpose(Math::Inverse(Math::ToMatrix3X3(transform.m_worldMatrix))));

        Resource::MaterialState state;
        if (auto* matCtx = Material::MaterialExecuteContext::Current())
        {
            const auto material = ResolveInstanceMaterial(world, *matCtx, entity);
            out.m_materialIndex = MaterialIndexOf(*matCtx, material);
            state = MaterialStateOf(*matCtx, material);
        }
        encoded.m_key = MakePipelineKey(0, state);
        out.m_pipelineKeyIndex = PipelineKeyIndexOf(encoded.m_key).m_value;

        const auto* geometrySlot = world.TryGet<GeometrySlot>(entity);
        const bool drawable = geometrySlot && world.Has<MeshGeometryReadyTag>(entity);
        out.m_geometryIndex = geometrySlot ? geometrySlot->Get() : 0;
        out.m_drawMask = drawable ? DrawListsOf(state) : 0;
        out.m_motionHistoryValid = historyValid && drawable ? 1u : 0u;
        if (const auto* slot = world.TryGet<EmissiveLightSlot>(entity))
        {
            out.m_lightSlotId = slot->m_slot.Get();
        }
        return encoded;
    }

    void InstanceBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        DeviceArray::Descriptor arrayDesc;
        arrayDesc.m_name        = ObjectName(SharedInput::Instances);
        arrayDesc.m_elementSize = sizeof(InstanceData);
        arrayDesc.m_capacity    = InstanceCapacity;
        arrayDesc.m_inputName   = RHI::InputName(SharedInput::Instances);
        arrayDesc.m_spaceId     = kInstanceSpaceId;
        if (m_instances.Init(rhiCtx, device, arrayDesc))
        {
            rhiCtx.Add<InstancePoolTag>(m_instances.GetResource());
        }

        m_instanceCountEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<ActiveInstanceCount>(m_instanceCountEntity, ActiveInstanceCount{ 0 });
        rhiCtx.Add<OpaqueSceneBounds>(m_instanceCountEntity, OpaqueSceneBounds{});

        if (m_activeList.Init(rhiCtx, device, ObjectName("InstanceIDBuffer"),
            RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::ShaderReadWrite))
        {
            rhiCtx.Add<InstanceIDBufferTag>(m_activeList.GetResource());
        }
    }

    void InstanceBindingSystem::SetUp(PassContext& passContext)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/ElementUpdate/ElementUpdate.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[InstanceBindingSystem] Failed to load shader ElementUpdate.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(passContext, "InstanceUpdate")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([this](ComputePassScopes& p)
            {
                const ElementUpdateBuffer& updates     = m_instances.GetUpdates();
                const uint32_t             updateCount = updates.GetUpdateCount();
                if (updateCount == 0)
                {
                    return;
                }

                const RHI::AttachmentId updatesName("InstanceUpdate");
                const RHI::AttachmentId instancesName("Instances");
                p.Import(updatesName, updates.GetResource());
                p.Import(instancesName, m_instances.GetResource());

                const uint32_t elementWords = updates.GetElementSize() / sizeof(uint32_t);

                auto s = p.Scope();
                s.ReadBuffer(updatesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(updates.GetByteCount())))
                    .Bind(RHI::InputName("g_Updates"));
                // Read too: the records not updated stay.
                s.ReadWriteBuffer(instancesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(m_instances.GetByteCount())))
                    .Bind(RHI::InputName("g_Elements"));
                s.Constant(RHI::InputName("updateCount"), updateCount);
                s.Constant(RHI::InputName("elementWords"), elementWords);
                s.Constant(RHI::InputName("indicesOffset"), static_cast<uint32_t>(updates.GetIndicesOffset()));
                s.Constant(RHI::InputName("elementsOffset"), static_cast<uint32_t>(updates.GetElementsOffset()));
                s.Dispatch(updateCount * elementWords);
                s.Close();
            })
            .Finalize();

        SPARK_COMPUTE_PASS(passContext, "InstanceIDUpdate")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([this](ComputePassScopes& p)
            {
                BuildInstanceIDUpdate(p, m_activeList,
                    RHI::AttachmentId("InstanceIDUpdate"), RHI::AttachmentId("InstanceIDs"));
            })
            .Finalize();
    }

    void InstanceBindingSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        AllocateSlots(*world);
        m_instances.BeginUpdate();
        const OpaqueSceneBounds opaqueBounds = UpdateInstanceData(*world);
        m_instances.EndUpdate();
        if (auto* drawCtx = DrawExecuteContext::Current())
        {
            m_keyCounter.Publish(*drawCtx);
        }

        CollectActiveSlots(*world);
        const uint32_t instanceCount = m_activeList.Update(m_activeSlots);
        if (m_instanceCountEntity != RHI::NullHandle)
        {
            rhiCtx->AddOrReplace<ActiveInstanceCount>(m_instanceCountEntity, ActiveInstanceCount{ instanceCount });
            rhiCtx->Get<OpaqueSceneBounds>(m_instanceCountEntity) = opaqueBounds;
        }

        // Capture only encoded instances, after every record has used the old history.
        // A mesh still uploading must not make its first drawable frame look historical.
        world->GetView<InstanceSlot, Transform::WorldTransformMatrix>(Exclude<DeadTag>).each(
            [&](Entity e, const InstanceSlot&, const Transform::WorldTransformMatrix& m)
        {
            const bool drawable = world->HasAll<GeometrySlot, MeshGeometryReadyTag>(e);
            world->AddOrReplace<InstanceHistory>(e, InstanceHistory{ m.m_worldMatrix, drawable });
            world->Remove<InstanceHistoryResetTag>(e);
        });
    }

    void InstanceBindingSystem::AllocateSlots(WorldContext& world)
    {
        // Collected first: InstanceSlot is the pool being iterated.
        eastl::vector<Entity> lostSource;
        world.GetView<InstanceSlot>().each([&](Entity entity, const InstanceSlot&)
        {
            if (!world.HasAll<Transform::WorldTransformMatrix, MeshGeometry>(entity))
            {
                lostSource.push_back(entity);
            }
        });
        for (Entity entity : lostSource)
        {
            world.Remove<InstanceSlot, InstanceHistory>(entity);
        }

        // Structural write inside iteration: InstanceSlot is only in the exclude set.
        world.GetView<Transform::WorldTransformMatrix, MeshGeometry>(Exclude<InstanceSlot, DeadTag>).each(
            [&](Entity entity, const Transform::WorldTransformMatrix&, const MeshGeometry&)
        {
            Suballocation range = m_instances.Allocate();
            if (!range.IsValid())
            {
                LOG_ERROR("[InstanceBindingSystem] All {} instance slots are taken; an entity is not drawn.", InstanceCapacity);
                return;
            }
            // A newly allocated slot must not inherit a stale transform snapshot.
            world.Remove<InstanceHistory>(entity);
            world.Add<InstanceSlot>(entity, InstanceSlot{ eastl::move(range) });
        });
    }

    OpaqueSceneBounds InstanceBindingSystem::UpdateInstanceData(WorldContext& world)
    {
        auto* drawCtx = DrawExecuteContext::Current();
        const auto& slots = world.GetStorage<InstanceSlot>();
        OpaqueSceneBounds opaqueBounds;
        world.GetView<InstanceSlot, Transform::WorldTransformMatrix, MeshGeometry>(Exclude<DeadTag>).each(
            [&](Entity e, const InstanceSlot& slot,
                const Transform::WorldTransformMatrix& transform, const MeshGeometry& geometry)
        {
            const EncodedInstance encoded = EncodeInstanceData(world, e);
            const InstanceData& out = encoded.m_data;
            if ((out.m_drawMask & DrawMaskOf<OpaqueTag>()) != 0)
            {
                const Math::AABB bounds = Math::TransformBounds(geometry.m_localBounds, out.m_model);
                if (opaqueBounds.m_valid)
                {
                    opaqueBounds.m_worldBounds.Expand(bounds.min);
                    opaqueBounds.m_worldBounds.Expand(bounds.max);
                }
                else
                {
                    opaqueBounds.m_worldBounds = bounds;
                    opaqueBounds.m_valid = true;
                }
            }
            m_keyCounter.Count(encoded.m_key, out.m_drawMask);
            if (drawCtx)
            {
                AddDrawListInstances(*drawCtx, out.m_drawMask, PositionOf(slots, e),
                    encoded.m_key, geometry, transform.m_worldMatrix);
            }
            m_instances.Set(slot.Get(), out);
        });
        return opaqueBounds;
    }

    void InstanceBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<InstanceSlot>();
        }
        m_instances.Shutdown(rhiCtx);

        if (m_instanceCountEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_instanceCountEntity); }
        m_instanceCountEntity = RHI::NullHandle;

        m_activeList.Shutdown(rhiCtx);
        m_activeSlots.clear();
    }

    void InstanceBindingSystem::CollectActiveSlots(WorldContext& world)
    {
        const auto& slots = world.GetStorage<InstanceSlot>();
        m_activeSlots.resize(slots.size());
        for (uint32_t position = 0; position < m_activeSlots.size(); ++position)
        {
            m_activeSlots[position] = SlotAt(slots, position);
        }
    }
}
