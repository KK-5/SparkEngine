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
    }

    void InstanceBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        // The layout of InstanceBindings.hlsli, declared rather than reflected (see SharedBindings.h).
        RHI::ShaderInputList inputs;
        inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
            RHI::InputName(InstanceBufferName), RHI::ShaderInputBufferAccess::Read,
            RHI::ShaderInputBufferType::Structured, 1, 0, 0, kInstanceSpaceId));

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Instances SRV is bound by StagedArrayBuffer once the upload
        // buffer materializes, so we DO NOT mark it dirty here.
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

        StagedArrayBuffer<InstanceData>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(InstanceBufferName);
        bufferDesc.m_inputName      = RHI::InputName(InstanceBufferName);
        bufferDesc.m_bindingsEntity = m_bindingsEntity;
        m_instances.Init(rhiCtx, bufferDesc);

        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        m_slotAllocator = new Suballocator(device, uint64_t(Capacity) * sizeof(InstanceData));

        m_slotCountEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<InstanceSlotCount>(m_slotCountEntity, InstanceSlotCount{ 0 });

        // Read as a vertex stream on the Graphics queue, where the pass of SetUp writes it.
        const uint64_t pageSize = device.GetLimits().m_reservedPageSizeInBytes;

        ReservedBuffer::Descriptor poolDesc;
        poolDesc.m_capacity  = InstanceIdByteCount;
        poolDesc.m_pageSize  = pageSize;
        poolDesc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::ShaderReadWrite;
        poolDesc.m_name      = ObjectName("InstanceIDBuffer");
        if (!m_instanceIdPool.Init(device, poolDesc))
        {
            LOG_ERROR("[InstanceBindingSystem] The instance ID buffer failed to init; no instance will be drawn.");
            return;
        }

        ElementUpdateBuffer::Descriptor updateDesc;
        updateDesc.m_name        = ObjectName("InstanceIDUpdate");
        updateDesc.m_elementSize = sizeof(uint32_t);
        if (!m_instanceIdUpdate.Init(device, updateDesc))
        {
            LOG_ERROR("[InstanceBindingSystem] The instance ID updates failed to init; no instance will be drawn.");
            m_instanceIdPool.Shutdown();
            return;
        }

        m_instanceIdResource = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::Buffer>(m_instanceIdResource, RHI::Components::Buffer{ Ptr<RHI::Buffer>(m_instanceIdPool.GetBuffer()) });
        rhiCtx.Add<RHI::ImportedTag>(m_instanceIdResource);
        rhiCtx.Add<RHI::ResourceName>(m_instanceIdResource, RHI::ResourceName{ poolDesc.m_name });
        rhiCtx.Add<InstanceIDBufferTag>(m_instanceIdResource);
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

        SPARK_COMPUTE_PASS(passContext, "InstanceIDUpdate")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([this](ComputePassScopes& p)
            {
                const uint32_t updateCount = m_instanceIdUpdate.GetUpdateCount();
                if (updateCount == 0)
                {
                    return;
                }

                const RHI::AttachmentId updatesName("InstanceIDUpdate");
                const RHI::AttachmentId idsName("InstanceIDs");
                p.Import(updatesName, m_instanceIdUpdate.GetResource());
                p.Import(idsName, m_instanceIdResource);

                const uint32_t elementWords = m_instanceIdUpdate.GetElementSize() / sizeof(uint32_t);

                auto s = p.Scope();
                s.ReadBuffer(updatesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(m_instanceIdUpdate.GetByteCount())))
                    .Bind(RHI::InputName("g_Updates"));
                // Read too: the IDs not updated stay.
                s.ReadWriteBuffer(idsName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(InstanceIdByteCount)))
                    .Bind(RHI::InputName("g_Elements"));
                s.Constant(RHI::InputName("updateCount"), updateCount);
                s.Constant(RHI::InputName("elementWords"), elementWords);
                s.Constant(RHI::InputName("indicesOffset"), static_cast<uint32_t>(m_instanceIdUpdate.GetIndicesOffset()));
                s.Constant(RHI::InputName("elementsOffset"), static_cast<uint32_t>(m_instanceIdUpdate.GetElementsOffset()));
                s.Dispatch(updateCount * elementWords);
                s.Close();
            })
            .Finalize();
    }

    void InstanceBindingSystem::Update(uint32_t frameIndex)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        // Nothing is allocated or encoded until the buffer materializes, as no record could
        // be read before.
        if (m_slotAllocator && m_instances.BindFrame(*rhiCtx, frameIndex))
        {
            UpdateSlots(*world);
            EncodeInstances(*world);
            m_instances.Upload(*rhiCtx, GetSlotCount());
        }
        if (auto* drawCtx = DrawExecuteContext::Current())
        {
            m_keyCounter.Publish(*drawCtx);
        }

        UpdateInstanceIds(GetSlotCount());

        if (m_slotCountEntity != RHI::NullHandle)
        {
            // Only the slots with an ID: a draw of any slot reads it. They are written this
            // frame ahead of every pass that draws.
            const uint32_t slotCount = eastl::min(GetSlotCount(), m_instanceIdCount);
            rhiCtx->AddOrReplace<InstanceSlotCount>(m_slotCountEntity, InstanceSlotCount{ slotCount });
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

    void InstanceBindingSystem::UpdateSlots(WorldContext& world)
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
            Suballocation range = m_slotAllocator->Allocate(sizeof(InstanceData));
            if (!range.IsValid())
            {
                LOG_ERROR("[InstanceBindingSystem] All {} instance slots are taken; an entity is not drawn.", Capacity);
                return;
            }
            world.Add<InstanceSlot>(entity, InstanceSlot{ eastl::move(range) });
        });
    }

    void InstanceBindingSystem::EncodeInstances(WorldContext& world)
    {
        // A slot no one holds reads as nothing: its record is cleared every frame, as the one
        // copy written this frame holds whatever was in it frameCountMax frames ago.
        InstanceData* records = m_instances.Data();
        eastl::fill(records, records + GetSlotCount(), InstanceData{});

        auto* matCtx  = Material::MaterialExecuteContext::Current();
        auto* drawCtx = DrawExecuteContext::Current();
        world.GetView<InstanceSlot, Transform::WorldTransformMatrix, MeshGeometry>(Exclude<DeadTag>).each(
            [&](Entity e, const InstanceSlot& slot,
                const Transform::WorldTransformMatrix& m, const MeshGeometry& geometry)
        {
            InstanceData& out = records[slot.Get()];
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
                AddDrawListInstances(*drawCtx, out.m_drawMask, slot.Get(), key, geometry, m.m_worldMatrix);
            }
        });
    }

    uint32_t InstanceBindingSystem::GetSlotCount() const
    {
        if (!m_slotAllocator)
        {
            return 0;
        }
        return static_cast<uint32_t>((m_slotAllocator->GetExtent() + sizeof(InstanceData) - 1) / sizeof(InstanceData));
    }

    void InstanceBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<InstanceSlot>();
        }
        // The slots just cleared hold it until the device hands them back.
        m_slotAllocator.reset();
        m_instances.Shutdown(rhiCtx);

        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }
        if (m_slotCountEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_slotCountEntity); }

        m_bindingsEntity = RHI::NullHandle;
        m_slotCountEntity = RHI::NullHandle;

        m_instanceIdCount = 0;
        if (m_instanceIdResource != RHI::NullHandle)
        {
            rhiCtx.DestoryEntity(m_instanceIdResource);
            m_instanceIdResource = RHI::NullHandle;
        }
        m_instanceIdUpdate.Shutdown();
        m_instanceIdPool.Shutdown();
    }

    void InstanceBindingSystem::UpdateInstanceIds(uint32_t slotCount)
    {
        if (!m_instanceIdPool.GetBuffer())
        {
            return;
        }

        // The IDs are found by their number, so they are a prefix of the buffer and so are their
        // pages: those past it are unmapped at once, as the queue runs the unmapping after every
        // read already submitted.
        const uint64_t pageSize    = m_instanceIdPool.GetPageSize();
        const uint32_t idsPerPage  = static_cast<uint32_t>(pageSize / sizeof(uint32_t));
        const uint32_t neededPages = (slotCount + idsPerPage - 1) / idsPerPage;
        const uint32_t mappedPages = m_instanceIdPool.GetMappedPageCount();

        m_instanceIdPageChanges.clear();
        for (uint32_t page = mappedPages; page < neededPages; ++page)
        {
            m_instanceIdPageChanges.push_back(Suballocator::PageChange{ page, true });
        }
        for (uint32_t page = neededPages; page < mappedPages; ++page)
        {
            m_instanceIdPageChanges.push_back(Suballocator::PageChange{ page, false });
        }
        m_instanceIdPool.Update(m_instanceIdPageChanges);

        // A page mapped again has a backing of its own, without the IDs it had.
        const uint64_t backedIds = uint64_t(m_instanceIdPool.GetMappedPageCount()) * idsPerPage;
        const uint32_t idCount   = static_cast<uint32_t>(eastl::min<uint64_t>(slotCount, backedIds));
        m_instanceIdCount        = static_cast<uint32_t>(eastl::min<uint64_t>(m_instanceIdCount, backedIds));
        const uint32_t newCount  = idCount > m_instanceIdCount ? idCount - m_instanceIdCount : 0;

        m_instanceIdUpdate.Begin(newCount);
        for (uint32_t id = m_instanceIdCount; id < idCount; ++id)
        {
            m_instanceIdUpdate.Set(id, id);
        }
        m_instanceIdCount += m_instanceIdUpdate.GetUpdateCount();
    }
}
