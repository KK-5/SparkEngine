#include "InstanceBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Shader/ShaderBuilder.h>
#include <Shader/ShaderBindingsUtils.h>

#include <Pass/Component/RHIComponents.h>   // CreateStaticBufferAttachment + Attachment enums

#include <Mesh/Components.h>
#include <Transform/Components.h>

#include "InstanceSlot.h"

namespace Spark::Render
{
    namespace
    {
        constexpr const char* InstanceBufferName = "g_Instances";
    }

    void InstanceBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[InstanceBindingSystem] AssetManager is unregistered.");

        // InstanceBindings.hlsl is a pure StructuredBuffer header with no entry point;
        // InstanceBindingsReflect.hlsl is a reflection host that #includes it and adds a
        // dummy vertex entry reading g_Instances, purely so we can reflect the space1
        // layout here (mirrors ViewBindingSystem / ViewBindingsReflect.hlsl).
        const Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/InstanceBindingsReflect.hlsl");
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

        ShaderInputBuildResult built = BuildShaderInputList(*shaderAsset);
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
        // only the handle. The g_Instances SRV is bound lazily in Update once the
        // upload buffer materializes, so we DO NOT mark it dirty here.
        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> instanceBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 1;   // InstanceBindings is reserved at space1.
        if (instanceBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[InstanceBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        m_bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindingsEntity, RHI::Components::ShaderBindings{ instanceBindings });
        rhiCtx.Add<InstanceBindingTag>(m_bindingsEntity);

        // g_Instances: host-visible (upload heap) StructuredBuffer, PER-FRAME (PerFrameTag
        // → N copies, one per frame-in-flight) so each frame writes its own copy without
        // racing the GPU reading last frame's (plan §G2). Filled each frame from
        // m_instanceData via PendingBufferMap (RHIResourceSystem::ProcessBufferMaps does
        // the Map/memcpy/Unmap and picks the current per-frame slot). ShaderRead lets the
        // SRV sit directly on the upload buffer. Materialized on the next frame begin.
        m_instanceData.resize(Capacity);
        m_bufferEntity = rhiCtx.CreateEntity();
        {
            RHI::BufferDescriptor bufDesc;
            bufDesc.m_byteCount = static_cast<uint64_t>(Capacity) * sizeof(InstanceData);
            bufDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor      = bufDesc;
            init.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
            rhiCtx.Add<RHI::PendingBufferInit>(m_bufferEntity, init);
            rhiCtx.Add<RHI::PerFrameTag>(m_bufferEntity);
            rhiCtx.Add<RHI::ResourceName>(m_bufferEntity, RHI::ResourceName{ ObjectName("g_Instances") });
        }

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

    bool InstanceBindingSystem::BindFrameInstances(uint32_t frameIndex)
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx || m_bufferEntity == RHI::NullHandle || m_bindingsEntity == RHI::NullHandle)
        {
            return false;
        }

        // Wait for RHIResourceSystem to materialize the per-frame buffers (one frame after Init).
        auto* perFrame = rhiCtx->TryGet<RHI::Components::BufferPerFrame>(m_bufferEntity);
        if (!perFrame || !perFrame->m_buffers[frameIndex])
        {
            return false;
        }
        RHI::Buffer* buffer = perFrame->m_buffers[frameIndex].get();

        // Re-bind the SRV to THIS frame's copy. The descriptor only encodes one buffer's
        // address, and the N copies live at distinct addresses, so this must happen every
        // frame (one cheap descriptor recompile via the dirty mark, like ViewBindings).
        const RHI::BufferViewDescriptor viewDesc =
            RHI::BufferViewDescriptor::CreateStructured(0, Capacity, sizeof(InstanceData));
        RHI::BufferView* view = RHI::GetOrCreateBufferViewPerFrame(
            *rhiCtx, m_bufferEntity, *buffer, viewDesc, frameIndex);
        if (!view)
        {
            LOG_ERROR("[InstanceBindingSystem] Failed to create g_Instances structured SRV for frame {}.", frameIndex);
            return false;
        }

        // Stages the view and marks the binding dirty for this frame's compile.
        SetShaderBuffer(m_bindingsEntity, RHI::InputName(InstanceBufferName), view);
        return true;
    }

    void InstanceBindingSystem::Update(uint32_t frameIndex)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        if (!BindFrameInstances(frameIndex))
        {
            // Buffers not ready yet (warmup frame). Emit no slots — consumers naturally
            // produce no draws this frame, which keeps the §11 contract intact.
            return;
        }

        // Clear last frame's slots first: the values are per-frame (§11), so a stale
        // slot left on a now-dead / non-renderable entity would be wrong. After this
        // rebuild, InstanceSlot exists ⟺ renderable this frame.
        world->Clear<InstanceSlot>();

        uint32_t idx = 0;
        world->GetView<Transform::WorldTransformMatrix, Mesh::MeshGPUComponent>(Exclude<DeadTag>)
            .each([&](Entity e, const Transform::WorldTransformMatrix& m, const Mesh::MeshGPUComponent&)
        {
            if (idx >= Capacity)
            {
                LOG_ERROR("[InstanceBindingSystem] Capacity={} overflow; dropping surplus instances.", Capacity);
                return;
            }

            m_instanceData[idx].m_model = m.m_worldMatrix;
            world->Add<InstanceSlot>(e, InstanceSlot{ idx });
            ++idx;
        });

        // Hand this frame's staged data to the host buffer. ProcessBufferMaps (in the
        // upcoming OnFrameBegin, after buffer creation) does Map → memcpy → Unmap and
        // removes the component, so we re-add it every frame. Only the written prefix is
        // copied; elements [idx, Capacity) are stale but never indexed by any slot.
        if (idx > 0)
        {
            rhiCtx->AddOrReplace<RHI::PendingBufferMap>(
                m_bufferEntity,
                RHI::PendingBufferMap{ m_instanceData.data(), 0, static_cast<size_t>(idx) * sizeof(InstanceData) });
        }
    }

    void InstanceBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }
        if (m_bufferEntity   != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bufferEntity); }
        if (m_idBufferEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_idBufferEntity); }

        m_bindingsEntity = RHI::NullHandle;
        m_bufferEntity   = RHI::NullHandle;
        m_idBufferEntity = RHI::NullHandle;
    }
}
