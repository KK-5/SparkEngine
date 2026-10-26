#include "InstanceBindingSystem.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <ECS/Common.h>
#include <Service/Service.h>

#include <RHI/HardwareQueue.h>
#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Pass/ComputePass.h>
#include <Pass/Component/RHIComponents.h>
#include <RenderGraph/PassScopes.h>

#include <Math/MathUtils.h>

#include <Material/MaterialUtils.h>         // MaterialComponent / StandardPBR / GetDefaultMaterial
#include <Binding/Material/MaterialBinding.h>   // MaterialSlotRef
#include <Binding/Geometry/GeometryBinding.h>
#include <Drawable/DrawMask.h>
#include <DrawList/DrawList.h>
#include <PipelineState/PipelineKeyIndex.h>

namespace Spark::Render
{
    namespace
    {
        constexpr const char* InstanceBufferName = "g_Instances";

        //! The material an object draws with. The one it overrides wins over the one it
        //! references; both are ordinary material entities by now. Falls back to the default
        //! material when unset or dangling.
        Material::MaterialHandle ResolveMaterial(WorldContext& world, Material::MaterialContext& matCtx, Entity e)
        {
            Material::MaterialHandle mh = Material::NullMaterial;
            if (const auto* ref = world.TryGet<MaterialOverrideRef>(e))
            {
                mh = ref->m_material;
            }
            else if (const auto* mc = world.TryGet<Material::MaterialComponent>(e))
            {
                mh = mc->m_material;
            }
            if (!(matCtx.Valid(mh) && matCtx.Has<Resource::StandardPBR>(mh)))
            {
                mh = Material::GetDefaultMaterial(matCtx);
            }
            return mh;
        }

        //! The material's stable g_Materials slot. Allocated once by MaterialBindingSystem and
        //! never moved, so this reads whatever is there rather than depending on a value
        //! written this same frame; a material whose slot does not exist yet reads 0 for a frame.
        uint32_t MaterialIndexOf(const Material::MaterialContext& matCtx, Material::MaterialHandle mh)
        {
            if (const auto* slotRef = matCtx.TryGet<MaterialSlotRef>(mh))
            {
                if (slotRef->IsValid())
                {
                    return slotRef->Get();
                }
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
    }

    void InstanceBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        // The layout of InstanceBindings.hlsli, declared rather than reflected (see SharedBindings.h).
        RHI::ShaderInputList inputs;
        inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
            RHI::InputName(InstanceBufferName), RHI::ShaderInputBufferAccess::Read,
            RHI::ShaderInputBufferType::Structured, 1, 0, 0, kInstanceSpaceId));

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The DeviceArray binds the g_Instances SRV.
        Ptr<RHI::ShaderBindings> instanceBindings =
            CreateSharedBindings(inputs, kInstanceSpaceId, "InstanceBindingSystem");
        if (!instanceBindings)
        {
            return;
        }

        m_bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindingsEntity, RHI::Components::ShaderBindings{ instanceBindings });
        rhiCtx.Add<InstanceBindingTag>(m_bindingsEntity);

        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        DeviceArray::Descriptor arrayDesc;
        arrayDesc.m_name        = ObjectName(InstanceBufferName);
        arrayDesc.m_elementSize = sizeof(InstanceData);
        arrayDesc.m_capacity    = InstanceCapacity;
        arrayDesc.m_inputName   = RHI::InputName(InstanceBufferName);
        arrayDesc.m_bindings    = m_bindingsEntity;
        if (m_instances.Init(rhiCtx, device, arrayDesc))
        {
            rhiCtx.Add<InstancePoolTag>(m_instances.GetResource());
        }

        m_instanceCountEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<ActiveInstanceCount>(m_instanceCountEntity, ActiveInstanceCount{ 0 });

        m_activeList.Init(rhiCtx, device);
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
                const ElementUpdateBuffer& updates     = m_activeList.GetUpdates();
                const uint32_t             updateCount = updates.GetUpdateCount();
                if (updateCount == 0)
                {
                    return;
                }

                const RHI::AttachmentId updatesName("InstanceIDUpdate");
                const RHI::AttachmentId idsName("InstanceIDs");
                p.Import(updatesName, updates.GetResource());
                p.Import(idsName, m_activeList.GetResource());

                const uint32_t elementWords = updates.GetElementSize() / sizeof(uint32_t);

                auto s = p.Scope();
                s.ReadBuffer(updatesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(updates.GetByteCount())))
                    .Bind(RHI::InputName("g_Updates"));
                // Read too: the entries not updated stay.
                s.ReadWriteBuffer(idsName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(ActiveInstanceList::ByteCount)))
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
        UpdateInstanceData(*world);
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
        }

        // Only encoded renderables hold a slot, so history starts on the frame they were first written.
        world->GetView<InstanceHistory, Transform::WorldTransformMatrix>(Exclude<DeadTag>).each(
            [](Entity, InstanceHistory& history, const Transform::WorldTransformMatrix& m)
        {
            history.m_model = m.m_worldMatrix;
        });
        world->GetView<InstanceSlot, Transform::WorldTransformMatrix>(Exclude<InstanceHistory, DeadTag>).each(
            [&](Entity e, const InstanceSlot&, const Transform::WorldTransformMatrix& m)
        {
            world->Add<InstanceHistory>(e, InstanceHistory{ m.m_worldMatrix });
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
            world.Remove<InstanceSlot>(entity);
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
            world.Add<InstanceSlot>(entity, InstanceSlot{ eastl::move(range) });
        });
    }

    void InstanceBindingSystem::UpdateInstanceData(WorldContext& world)
    {
        auto* matCtx  = Material::MaterialExecuteContext::Current();
        auto* drawCtx = DrawExecuteContext::Current();
        const auto& slots = world.GetStorage<InstanceSlot>();
        world.GetView<InstanceSlot, Transform::WorldTransformMatrix, MeshGeometry>(Exclude<DeadTag>).each(
            [&](Entity e, const InstanceSlot& slot,
                const Transform::WorldTransformMatrix& m, const MeshGeometry& geometry)
        {
            InstanceData out;
            const auto* history = world.TryGet<InstanceHistory>(e);
            out.m_model     = m.m_worldMatrix;
            out.m_prevModel = history ? history->m_model : m.m_worldMatrix;
            // Normals need the inverse-transpose of the model's linear part to stay
            // perpendicular under non-uniform scale (tangents do not — they keep the plain
            // model matrix in the VS). Precomputed here so the VS avoids a per-vertex 3x3
            // inverse; embedded in a 4x4 for a StructuredBuffer-safe layout (VS reads its 3x3).
            out.m_normalMatrix = Math::ToMatrix4X4(
                Math::Transpose(Math::Inverse(Math::ToMatrix3X3(m.m_worldMatrix))));
            Resource::MaterialState state;
            out.m_materialIndex = 0;
            if (matCtx)
            {
                const Material::MaterialHandle material = ResolveMaterial(world, *matCtx, e);
                out.m_materialIndex = MaterialIndexOf(*matCtx, material);
                state               = MaterialStateOf(*matCtx, material);
            }
            // The one material shader there is: every material is StandardPBR.
            const PipelineKey key  = MakePipelineKey(0, state);
            out.m_pipelineKeyIndex = PipelineKeyIndexOf(key).m_value;
            // 0 until the geometry can be read and has its g_Geometries record: a reader of
            // g_Instances draws what is set, from the record m_geometryIndex names.
            const auto* geometrySlot = world.TryGet<GeometrySlotRef>(e);
            const bool  drawable     = geometrySlot && world.Has<MeshGeometryReadyTag>(e);
            out.m_geometryIndex = geometrySlot ? geometrySlot->Get() : 0;
            out.m_drawMask      = drawable ? DrawListsOf(state) : 0;
            // What the culling can pick: counted by key where the GPU culls, listed one by one
            // where the CPU does.
            m_keyCounter.Count(key, out.m_drawMask);
            if (drawCtx)
            {
                AddDrawListInstances(*drawCtx, out.m_drawMask, PositionOf(slots, e), key, geometry, m.m_worldMatrix);
            }
            m_instances.Set(slot.Get(), out);
        });
    }

    void InstanceBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<InstanceSlot>();
        }
        m_instances.Shutdown(rhiCtx);

        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }
        if (m_instanceCountEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_instanceCountEntity); }

        m_bindingsEntity = RHI::NullHandle;
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
