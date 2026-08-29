#include "InstanceBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <ECS/Common.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <Pass/Component/RHIComponents.h>   // CreateStaticBufferAttachment + Attachment enums

#include <Math/MathUtils.h>

#include <Material/MaterialUtils.h>         // MaterialComponent / StandardPBR / GetDefaultMaterial
#include <Binding/Material/MaterialBinding.h>   // MaterialSlotRef

namespace Spark::Render
{
    namespace
    {
        constexpr const char* InstanceBufferName = "g_Instances";

        //! MaterialComponent -> material handle (falling back to the default material when
        //! unset or dangling) -> its stable g_Materials slot. The slot is allocated once
        //! by MaterialBindingSystem and does not move, so this reads whatever is there
        //! rather than depending on a value written this same frame; a material whose
        //! slot does not exist yet falls back to 0 for a frame.
        uint32_t ResolveMaterialIndex(
            WorldContext& world, Material::MaterialContext* matCtx, Entity e)
        {
            if (!matCtx)
            {
                return 0;
            }

            Material::MaterialHandle mh = Material::NullMaterial;
            if (const auto* mc = world.TryGet<Material::MaterialComponent>(e))
            {
                mh = mc->m_material;
            }
            if (!(matCtx->Valid(mh) && matCtx->Has<Resource::StandardPBR>(mh)))
            {
                mh = Material::GetDefaultMaterial(*matCtx);
            }
            if (const auto* slotRef = matCtx->TryGet<MaterialSlotRef>(mh))
            {
                if (slotRef->IsValid())
                {
                    return slotRef->m_id;
                }
            }
            return 0;
        }
    }

    void InstanceBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[InstanceBindingSystem] AssetManager is unregistered.");

        // InstanceBindings.hlsl is a pure StructuredBuffer header with no entry point;
        // InstanceBindingsReflect.hlsl is a reflection host that #includes it and adds a
        // dummy vertex entry reading g_Instances, purely so we can reflect the space4
        // layout here (mirrors ViewBindingSystem / ViewBindingsReflect.hlsl).
        const Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/InstanceBindingsReflect.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[InstanceBindingSystem] Failed to resolve InstanceBindingsReflect.hlsl asset id.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        if (!shaderAsset)
        {
            LOG_ERROR("[InstanceBindingSystem] Failed to load InstanceBindingsReflect.hlsl.");
            return;
        }

        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[InstanceBindingSystem] InstanceBindings.hlsl produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[InstanceBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[InstanceBindingSystem] RHI factory or device is null.");

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Instances SRV is bound by GlobalBuffer once the upload
        // buffer materializes, so we DO NOT mark it dirty here.
        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> instanceBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 4;   // InstanceBindings (per-object) is reserved at space4.
        if (instanceBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[InstanceBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        m_bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindingsEntity, RHI::Components::ShaderBindings{ instanceBindings });
        rhiCtx.Add<InstanceBindingTag>(m_bindingsEntity);

        GlobalBuffer<Instances, InstanceData,
                     Transform::WorldTransformMatrix, Mesh::MeshGPUComponent>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(InstanceBufferName);
        bufferDesc.m_inputName      = RHI::InputName(InstanceBufferName);
        bufferDesc.m_bindingsEntity = m_bindingsEntity;
        m_instances.Init(rhiCtx, bufferDesc);

        // Per-instance vertex-stream ID buffer: static identity table [0..Capacity-1],
        // uploaded once and read as a vertex stream every frame. It goes through the
        // StaticImported path (CreateStaticBuffer + static buffer attachment) so the
        // render graph emits the one-time upload→InputAssembly barrier AND the
        // copy→graphics fence wait via CompileStaticResourceBarriers — without the
        // attachment the graphics queue would race the async upload. m_idData is kept
        // alive for the upload's lifetime (PendingBufferUpload contract).
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

            m_idBufferEntity = RHI::CreateStaticBuffer(rhiCtx, ObjectName("InstanceIDBuffer"), bufDesc);
            RHI::RequestBufferUpload(
                rhiCtx, m_idBufferEntity, m_idData.data(), m_idData.size() * sizeof(uint32_t));
            CreateStaticBufferAttachment(rhiCtx, m_idBufferEntity,
                RHI::InputName("InstanceIDBuffer"),
                RHI::AttachmentAccess::Read,
                RHI::AttachmentUsage::InputAssembly,
                RHI::AttachmentStage::VertexInput);
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
        m_instances.Update(*world, *rhiCtx, frameIndex,
            [&](Entity e, InstanceData& out,
                const Transform::WorldTransformMatrix& m, const Mesh::MeshGPUComponent&)
        {
            out.m_model = m.m_worldMatrix;
            // Normals need the inverse-transpose of the model's linear part to stay
            // perpendicular under non-uniform scale (tangents do not — they keep the plain
            // model matrix in the VS). Precomputed here so the VS avoids a per-vertex 3x3
            // inverse; embedded in a 4x4 for a StructuredBuffer-safe layout (VS reads its 3x3).
            out.m_normalMatrix = Math::ToMatrix4X4(
                Math::Transpose(Math::Inverse(Math::ToMatrix3X3(m.m_worldMatrix))));
            out.m_materialIndex = ResolveMaterialIndex(*world, matCtx, e);
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

        m_bindingsEntity = RHI::NullHandle;
        m_idBufferEntity = RHI::NullHandle;
    }
}
