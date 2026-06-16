#include "DepthPreProcessor.h"

#include <ECS/Common.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawArguments.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>

#include <Request/DrawRequest.h>

#include <View/View.h>

#include <Mesh/Components.h>
#include <Transform/Components.h>

namespace Spark::Render
{
    void DepthPreProcessor::Shutdown(PassContext& passCtx)
    {
        // Release our Ptr ref to the ShaderBindings first.
        m_viewBindings = nullptr;

        // Detach the pass-level ShaderBindings so the pass entity releases its Ptr.
        AttachShaderBindings<SPARK_PASS_TAG("DepthPrePass")>(passCtx, 0, nullptr);

        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (rhiCtx)
        {
            // Destroy the RHI entity that holds the ShaderBindings component.
            if (m_viewBindingsEntity != RHI::NullHandle)
            {
                rhiCtx->DestoryEntity(m_viewBindingsEntity);
                m_viewBindingsEntity = RHI::NullHandle;
            }

            // Collect and destroy all DrawRequest entities tagged with this pass.
            {
                eastl::vector<RHIHandle> toDestroy;
                rhiCtx->GetView<DrawRequest, SPARK_PASS_TAG("DepthPrePass")>().each(
                    [&](RHIHandle entity, const DrawRequest&) { toDestroy.push_back(entity); });
                for (auto entity : toDestroy)
                {
                    rhiCtx->DestoryEntity(entity);
                }
            }

            // Collect and destroy all per-draw model ShaderBindings entities.
            {
                eastl::vector<RHIHandle> toDestroy;
                rhiCtx->GetView<RHI::Components::ShaderBindings, SPARK_PASS_TAG("DepthPrePass")>().each(
                    [&](RHIHandle entity, const RHI::Components::ShaderBindings&) { toDestroy.push_back(entity); });
                for (auto entity : toDestroy)
                {
                    rhiCtx->DestoryEntity(entity);
                }
            }
        }
    }

    void DepthPreProcessor::Init(PassContext& passCtx, RHI::RHIContext& rhiCtx)
    {
        auto handle = CreatePassShaderBindings<SPARK_PASS_TAG("DepthPrePass")>(
            passCtx, rhiCtx, /*spaceId*/ 0);
        if (!handle.m_bindings)
        {
            LOG_ERROR("[DepthPreProcessor] Failed to create view bindings for DepthPrePass.");
            return;
        }

        m_viewBindings       = handle.m_bindings;
        m_viewBindingsEntity = handle.m_entity;

        AttachShaderBindings<SPARK_PASS_TAG("DepthPrePass")>(
            passCtx, /*spaceId*/ 0, m_viewBindings);
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

        if (m_viewBindings && m_viewBindingsEntity != RHI::NullHandle)
        {
            const float aspect = (renderSize.y > 0)
                ? static_cast<float>(renderSize.x) / static_cast<float>(renderSize.y)
                : 1.0f;

            View view = MakePerspectiveView(
                Math::Vector3(0.f, 5.f, -5.f),
                Math::Vector3(0.f, 0.f, 0.f),
                Math::Vector3(0.f, 1.f, 0.f),
                Math::Radians(45.f), aspect, 0.1f, 100.f);

            WriteViewConstants(view, *m_viewBindings);
            MarkShaderBindingsUpdate(*rhiCtx, m_viewBindingsEntity);
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

            // Per-draw ShaderBindings at space1: model matrix from Transform system.
            auto modelHandle = CreatePassShaderBindings<SPARK_PASS_TAG("DepthPrePass")>(
                *passCtx, *rhiCtx, /*spaceId*/ 1);

            if (modelHandle.m_bindings)
            {
                auto* modelInput = modelHandle.m_bindings->FindConstantInput(
                    RHI::InputName("g_Model"));
                if (modelInput)
                {
                    modelInput->SetData(&worldMatrix.m_worldMatrix, sizeof(worldMatrix.m_worldMatrix));
                }

                rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(modelHandle.m_entity);
                req.m_shaderBindingEntities.push_back(modelHandle.m_entity);
            }

            rhiCtx->Add<DrawRequest>(drawEntity, eastl::move(req));
            rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(drawEntity);

            world->Add<SPARK_PASS_TAG("DepthPrePass")>(entity);
            world->Add<DrawEntity>(entity, drawEntity);
            world->Add<MatrixBindEntity>(entity, modelHandle.m_entity);
        });

        world->GetView<SPARK_PASS_TAG("DepthPrePass"), Transform::WorldTransformMatrix, MatrixBindEntity>().each(
            [&](Entity entity, const Transform::WorldTransformMatrix& worldMatrix, const MatrixBindEntity& bind)
        {
            auto shaderBindings = rhiCtx->Get<RHI::Components::ShaderBindings>(bind.m_binding);
            auto* modelInput = shaderBindings.m_bindings->FindConstantInput(RHI::InputName("g_Model"));
            if (modelInput)
            {
                modelInput->SetData(&worldMatrix.m_worldMatrix, sizeof(worldMatrix.m_worldMatrix));
            }
            if (!rhiCtx->Has<RHI::ShaderBindingsUpdateTag>(bind.m_binding))
            {
                rhiCtx->Add<RHI::ShaderBindingsUpdateTag>(bind.m_binding);
            }
        });
    }
}
