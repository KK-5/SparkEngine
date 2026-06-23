#include "DepthPreProcessor.h"

#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawArguments.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/Component/RHIComponents.h>   // Render-namespace RHIHandle / NullHandle aliases

#include <Request/DrawRequest.h>
#include <Resource/Shader/ShaderAsset.h>   // complete type for DrawRequest's Ptr<ShaderAsset> dtor

#include <View/ViewTags.h>
#include <Instance/InstanceSlot.h>

#include <Mesh/Components.h>

namespace Spark::Render
{
    void DepthPreProcessor::Shutdown(PassContext& /*passCtx*/)
    {
        // Reap the last frame's transient DrawRequests (the per-view / per-instance
        // bindings and the ID buffer are owned by their systems — not ours to free).
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (rhiCtx)
        {
            eastl::vector<RHIHandle> stale;
            rhiCtx->GetView<DrawRequest, SPARK_PASS_TAG("DepthPrePass")>().each(
                [&](RHIHandle entity, const DrawRequest&) { stale.push_back(entity); });
            for (RHIHandle entity : stale)
            {
                rhiCtx->DestoryEntity(entity);
            }
        }
    }

    void DepthPreProcessor::Init(PassContext& /*passCtx*/, RHI::RHIContext& /*rhiCtx*/)
    {
        // Nothing to set up: the per-view (space0) and per-instance (space1) bindings
        // and the per-instance ID vertex stream are shared resources produced by
        // ViewBindingSystem / InstanceBindingSystem. Each frame's DrawRequests just
        // reference them via m_shaderBindingEntities / m_instanceIdBuffer.
    }

    void DepthPreProcessor::Process(const Math::Vector2Int& renderSize)
    {
        auto* world = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        if (!world || !rhiCtx || !passCtx)
        {
            return;
        }

        // Per-frame transient model: reap the previous frame's DrawRequests (CPU-only —
        // the GPU consumed last frame's DrawItems already), then rebuild from scratch.
        // Destroy after collecting, never during the view iteration.
        {
            eastl::vector<RHIHandle> stale;
            rhiCtx->GetView<DrawRequest, SPARK_PASS_TAG("DepthPrePass")>().each(
                [&](RHIHandle entity, const DrawRequest&) { stale.push_back(entity); });
            for (RHIHandle entity : stale)
            {
                rhiCtx->DestoryEntity(entity);
            }
        }

        // Shared per-view (space0) binding, produced/refreshed by ViewBindingSystem.
        RHIHandle viewBindingEntity = RHI::NullHandle;
        rhiCtx->GetView<MainViewTag, RHI::Components::ShaderBindings>().each(
            [&](RHIHandle e, const RHI::Components::ShaderBindings&) { viewBindingEntity = e; });

        // Shared per-instance (space1) g_Instances binding, from InstanceBindingSystem.
        RHIHandle instanceBindingEntity = RHI::NullHandle;
        rhiCtx->GetView<InstanceBindingTag, RHI::Components::ShaderBindings>().each(
            [&](RHIHandle e, const RHI::Components::ShaderBindings&) { instanceBindingEntity = e; });

        // Shared per-instance ID vertex stream, from InstanceBindingSystem.
        RHIHandle idBufferEntity = RHI::NullHandle;
        uint32_t  idBufferByteCount = 0;
        rhiCtx->GetView<InstanceIDBufferTag, RHI::Components::Buffer>().each(
            [&](RHIHandle e, const RHI::Components::Buffer& buf)
        {
            idBufferEntity    = e;
            idBufferByteCount = buf.m_buffer ? static_cast<uint32_t>(buf.m_buffer->GetDescriptor().m_byteCount) : 0;
        });

        // Any missing → warmup frame (the shared resources materialize one frame after
        // Init). InstanceSlot also won't exist yet, so there is nothing to draw.
        if (viewBindingEntity == RHI::NullHandle
            || instanceBindingEntity == RHI::NullHandle
            || idBufferEntity == RHI::NullHandle
            || idBufferByteCount == 0)
        {
            return;
        }

        // Renderable set THIS frame == entities carrying an InstanceSlot (plan §11).
        // DepthPre touches no world-layer concept (Transform / WorldTransformMatrix);
        // the slot is the only — and GPU-layer — coupling point.
        world->GetView<Mesh::MeshGPUComponent, InstanceSlot>().each(
            [&](Entity, const Mesh::MeshGPUComponent& gpu, const InstanceSlot& slot)
        {
            if (gpu.m_vertexBuffer == RHI::NullHandle)
            {
                return;
            }
            if (gpu.m_vertexByteCount == 0 || gpu.m_vertexByteStride == 0)
            {
                return;
            }

            RHIHandle drawEntity = rhiCtx->CreateEntity();

            DrawRequest req;
            // InstanceCount = 1 (un-batched); StartInstanceLocation = the entity's
            // per-frame slot — the entry point for InstanceIndex into the shader.
            req.m_drawInstanceArgs = RHI::DrawInstanceArguments(1, slot.m_index);

            // slot 0: mesh vertex buffer.
            req.m_vertexBuffer = gpu.m_vertexBuffer;
            req.m_vertexBufferInfo = VertexBufferInfo{
                0, gpu.m_vertexByteCount, gpu.m_vertexByteStride};

            // slot 1: shared per-instance ID stream (full buffer; IA reads element
            // [StartInstanceLocation]). Stride = one uint per instance.
            req.m_instanceIdBuffer = idBufferEntity;
            req.m_instanceIdBufferInfo = VertexBufferInfo{
                0, idBufferByteCount, static_cast<uint32_t>(sizeof(uint32_t))};

            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                req.m_drawArguments = RHI::DrawArguments(
                    RHI::DrawIndexed(0, gpu.m_indexCount, 0));
                req.m_indexBuffer = gpu.m_indexBindings;
                req.m_indexBufferInfo = IndexBufferInfo{
                    0, gpu.m_indexByteCount, gpu.m_indexFormat};
            }
            else
            {
                const uint32_t vertexCount = gpu.m_vertexByteCount / gpu.m_vertexByteStride;
                req.m_drawArguments = RHI::DrawArguments(
                    RHI::DrawLinear(0, vertexCount));
            }

            req.m_viewports.resize(1);
            req.m_scissors.resize(1);
            req.m_viewportsCount = 1;
            req.m_viewports[0]   = RHI::Viewport{0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y)};
            req.m_scissorsCount  = 1;
            req.m_scissors[0]    = RHI::Scissor{0, 0, renderSize.x, renderSize.y};

            // Shared bindings, in space order: space0 view, space1 g_Instances.
            req.m_shaderBindingEntities.push_back(viewBindingEntity);
            req.m_shaderBindingEntities.push_back(instanceBindingEntity);

            rhiCtx->Add<DrawRequest>(drawEntity, eastl::move(req));
            rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(drawEntity);
        });
    }
}
