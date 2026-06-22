#include "DepthPreProcessor.h"

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawArguments.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>

#include <Request/DrawRequest.h>

#include <Shader/ShaderBindingsUtils.h>
#include <View/ViewTags.h>

#include <Mesh/Components.h>
#include <Transform/Components.h>

namespace Spark::Render
{
    void DepthPreProcessor::Shutdown(PassContext& /*passCtx*/)
    {
        // The view binding is owned by ViewBindingSystem (MainViewTag) — not ours to
        // tear down. We only destroy the entities this processor created: the
        // DrawRequests and the per-draw model ShaderBindings.
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (rhiCtx)
        {
            rhiCtx->GetView<DrawRequest, SPARK_PASS_TAG("DepthPrePass")>().each(
                [&](RHIHandle entity, const DrawRequest&) { rhiCtx->Add<DeadTag>(entity); });

            rhiCtx->GetView<RHI::Components::ShaderBindings, SPARK_PASS_TAG("DepthPrePass")>().each(
                [&](RHIHandle entity, const RHI::Components::ShaderBindings&) { rhiCtx->Add<DeadTag>(entity); });
        }
    }

    void DepthPreProcessor::Init(PassContext& /*passCtx*/, RHI::RHIContext& /*rhiCtx*/)
    {
        // Nothing to set up: the per-view (space0) binding is the shared ViewBindings
        // entity produced by ViewBindingSystem (MainViewTag); per-draw model (space1)
        // bindings are created on demand in Process. All shader inputs flow through
        // each DrawRequest's m_shaderBindingEntities — no pass-level AttachShaderBindings.
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

        // The shared per-view (space0) binding, produced and refreshed by
        // ViewBindingSystem before Processors run. We only reference it.
        RHIHandle viewBindingEntity = RHI::NullHandle;
        rhiCtx->GetView<MainViewTag, RHI::Components::ShaderBindings>().each(
            [&](RHIHandle e, const RHI::Components::ShaderBindings&) { viewBindingEntity = e; });
        if (viewBindingEntity == RHI::NullHandle)
        {
            LOG_ERROR("[DepthPreProcessor] No MainViewTag view binding found; ViewBindingSystem not initialized?");
            return;
        }

        world->GetView<Mesh::MeshGPUComponent, Transform::WorldTransformMatrix>(Exclude<SPARK_PASS_TAG("DepthPrePass")>).each(
            [&](Entity entity, const Mesh::MeshGPUComponent& gpu, const Transform::WorldTransformMatrix& worldMatrix)
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
            req.m_drawInstanceArgs = RHI::DrawInstanceArguments(1, 0);
            req.m_vertexBuffer = gpu.m_vertexBuffer;
            req.m_vertexBufferInfo = VertexBufferInfo{
                0, gpu.m_vertexByteCount, gpu.m_vertexByteStride};

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

            // Shared per-view (space0) binding: just a reference into this draw.
            req.m_shaderBindingEntities.push_back(viewBindingEntity);

            // Per-draw ShaderBindings at space1: model matrix from Transform system.
            RHIHandle modelEntity = CreatePassShaderBindings<SPARK_PASS_TAG("DepthPrePass")>(
                *passCtx, *rhiCtx, /*spaceId*/ 1);

            if (modelEntity != RHI::NullHandle)
            {
                SetShaderConstant(modelEntity, RHI::InputName("g_Model"), worldMatrix.m_worldMatrix);

                rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(modelEntity);
                req.m_shaderBindingEntities.push_back(modelEntity);
            }

            rhiCtx->Add<DrawRequest>(drawEntity, eastl::move(req));
            rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(drawEntity);

            world->Add<SPARK_PASS_TAG("DepthPrePass")>(entity);
            world->Add<DrawEntity>(entity, drawEntity);
            world->Add<MatrixBindEntity>(entity, modelEntity);
        });

        world->GetView<SPARK_PASS_TAG("DepthPrePass"), Transform::WorldTransformMatrix, MatrixBindEntity>().each(
            [&](Entity entity, const Transform::WorldTransformMatrix& worldMatrix, const MatrixBindEntity& bind)
        {
            // SetShaderConstant resolves the entity's binding, stages the data, and
            // marks it dirty for the next compile.
            SetShaderConstant(bind.m_binding, RHI::InputName("g_Model"), worldMatrix.m_worldMatrix);
        });
    }
}
