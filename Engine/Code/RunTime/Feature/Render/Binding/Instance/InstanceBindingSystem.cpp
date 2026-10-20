#include "InstanceBindingSystem.h"

#include <Log/ILogSystem.h>
#include <ECS/Common.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <RHI/ResourceBuilder.h>
#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Pass/Component/RHIComponents.h>

#include <Math/MathUtils.h>

#include <Material/MaterialUtils.h>         // MaterialComponent / StandardPBR / GetDefaultMaterial
#include <Binding/Material/MaterialBinding.h>   // MaterialSlotRef
#include <Binding/Geometry/GeometryBinding.h>
#include <Drawable/DrawMask.h>
#include <DrawBin/DrawBinSystem.h>
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

        PipelineKey PipelineKeyOf(const Material::MaterialContext& matCtx, Material::MaterialHandle mh)
        {
            // The one material shader there is: every material is StandardPBR.
            const auto* state = matCtx.TryGet<Resource::MaterialState>(mh);
            return MakePipelineKey(0, state ? *state : Resource::MaterialState{});
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
        // only the handle. The g_Instances SRV is bound by GlobalBuffer once the upload
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

        GlobalBuffer<Instances, InstanceData,
                     Transform::WorldTransformMatrix, MeshGeometry>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(InstanceBufferName);
        bufferDesc.m_inputName      = RHI::InputName(InstanceBufferName);
        bufferDesc.m_bindingsEntity = m_bindingsEntity;
        m_instances.Init(rhiCtx, bufferDesc);

        m_slotCountEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<InstanceSlotCount>(m_slotCountEntity, InstanceSlotCount{ 0 });

        // Per-instance vertex-stream ID buffer: static identity table [0..Capacity-1],
        // uploaded once and read as a vertex stream every frame, once it is ready (Update).
        // m_idData is kept alive for the upload's lifetime (BufferUploadRequest contract).
        {
            m_idData.resize(Capacity);
            for (uint32_t i = 0; i < Capacity; ++i)
            {
                m_idData[i] = i;
            }

            RHI::BufferDescriptor bufDesc;
            bufDesc.m_byteCount       = static_cast<uint64_t>(Capacity) * sizeof(uint32_t);
            bufDesc.m_bindFlags       = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
            bufDesc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

            m_idBufferEntity = RHI::CreateBuffer(rhiCtx, ObjectName("InstanceIDBuffer"), bufDesc);
            RHI::RequestBufferUpload(
                rhiCtx, m_idBufferEntity, m_idData.data(), m_idData.size() * sizeof(uint32_t));
            rhiCtx.Add<InstanceIDBufferTag>(m_idBufferEntity);
        }
    }

    void InstanceBindingSystem::Update(uint32_t frameIndex)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        auto* matCtx = Material::MaterialExecuteContext::Current();
        m_keyCounts.fill(KeyInstanceCount{});
        m_instances.Update(*world, *rhiCtx, frameIndex,
            [&](Entity e, InstanceData& out,
                const Transform::WorldTransformMatrix& m, const MeshGeometry&)
        {
            const auto* history = world->TryGet<InstanceHistory>(e);
            out.m_model     = m.m_worldMatrix;
            out.m_prevModel = history ? history->m_model : m.m_worldMatrix;
            // Normals need the inverse-transpose of the model's linear part to stay
            // perpendicular under non-uniform scale (tangents do not — they keep the plain
            // model matrix in the VS). Precomputed here so the VS avoids a per-vertex 3x3
            // inverse; embedded in a 4x4 for a StructuredBuffer-safe layout (VS reads its 3x3).
            out.m_normalMatrix = Math::ToMatrix4X4(
                Math::Transpose(Math::Inverse(Math::ToMatrix3X3(m.m_worldMatrix))));
            PipelineKey key;
            if (matCtx)
            {
                const Material::MaterialHandle material = ResolveMaterial(*world, *matCtx, e);
                out.m_materialIndex = MaterialIndexOf(*matCtx, material);
                key                 = PipelineKeyOf(*matCtx, material);
            }
            else
            {
                out.m_materialIndex = 0;
            }
            out.m_pipelineKeyIndex = PipelineKeyIndexOf(key).m_value;
            // 0 until the geometry can be read and has its g_Geometries record: a reader of
            // g_Instances draws what is set, from the record m_geometryIndex names.
            const auto* geometrySlot = world->TryGet<GeometrySlotRef>(e);
            const bool  drawable     = geometrySlot && world->Has<MeshGeometryReadyTag>(e);
            out.m_geometryIndex = geometrySlot ? geometrySlot->Get() : 0;
            out.m_drawMask      = drawable ? ClassifyDraw(*world, e) : 0;
            // What the culling can pick, so what the bins are sized for.
            CountKeyInstance(m_keyCounts, key, out.m_drawMask);
        });
        PublishKeyInstanceCounts(*rhiCtx, m_keyCounts);

        if (m_slotCountEntity != RHI::NullHandle)
        {
            // No slot until the ID buffer's upload is over: a draw of any slot reads it.
            const uint32_t slotCount = RHI::IsResourceReady(*rhiCtx, m_idBufferEntity) ? m_instances.Size() : 0;
            rhiCtx->AddOrReplace<InstanceSlotCount>(m_slotCountEntity, InstanceSlotCount{ slotCount });
        }

        // Only encoded renderables hold a slot, so history starts on the frame they were first written.
        world->GetView<InstanceHistory, Transform::WorldTransformMatrix>(Exclude<DeadTag>).each(
            [](Entity, InstanceHistory& history, const Transform::WorldTransformMatrix& m)
        {
            history.m_model = m.m_worldMatrix;
        });
        world->GetView<InstanceSlotRef, Transform::WorldTransformMatrix>(Exclude<InstanceHistory, DeadTag>).each(
            [&](Entity e, const InstanceSlotRef&, const Transform::WorldTransformMatrix& m)
        {
            world->Add<InstanceHistory>(e, InstanceHistory{ m.m_worldMatrix });
        });
    }

    void InstanceBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            m_instances.Shutdown(*world, rhiCtx);
        }

        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }
        if (m_idBufferEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_idBufferEntity); }
        if (m_slotCountEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_slotCountEntity); }

        m_bindingsEntity = RHI::NullHandle;
        m_idBufferEntity = RHI::NullHandle;
        m_slotCountEntity = RHI::NullHandle;
    }
}
