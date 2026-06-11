#include "DepthPreProcessor.h"

#include <ECS/Common.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawArguments.h>
#include <RHI/Resource/Buffer/Buffer.h>

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

            // Destroy all DrawRequest entities created during Process().
            for (auto& [meshEntity, drawEntity] : m_meshToDrawRequest)
            {
                if (rhiCtx->Valid(drawEntity))
                {
                    rhiCtx->DestoryEntity(drawEntity);
                }
            }

            // Destroy all per-draw model ShaderBindings entities.
            for (auto& [meshEntity, bindEntity] : m_meshToModelBinding)
            {
                if (rhiCtx->Valid(bindEntity))
                {
                    rhiCtx->DestoryEntity(bindEntity);
                }
            }
        }
        m_meshToDrawRequest.clear();
        m_meshToModelBinding.clear();
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
        if (!world || !rhiCtx)
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

        // Snapshot MeshGPUComponent data first to avoid holding references
        // into the component pool across RHI entity creation. The asset worker
        // thread may call AddOrReplace<MeshGPUComponent> during iteration,
        // which can reallocate the packed array and invalidate references.
        eastl::vector<eastl::pair<Entity, Mesh::MeshGPUComponent>> snapshot;
        world->GetView<Mesh::MeshGPUComponent>().each(
            [&](Entity entity, const Mesh::MeshGPUComponent& gpu)
            {
                snapshot.emplace_back(entity, gpu);
            });

        for (const auto& [entity, gpu] : snapshot)
        {
            if (m_meshToDrawRequest.find(entity) != m_meshToDrawRequest.end())
            {
                continue;
            }

            if (gpu.m_vertexBuffer == RHI::NullHandle)
            {
                continue;
            }

            auto* vbComp = rhiCtx->TryGet<RHI::Components::Buffer>(gpu.m_vertexBuffer);
            if (!vbComp || !vbComp->m_buffer)
            {
                continue;
            }

            const uint64_t vbByteCount = vbComp->m_buffer->GetDescriptor().m_byteCount;
            if (vbByteCount == 0)
            {
                continue;
            }

            auto streamBuffers = gpu.m_inputLayout.GetStreamBuffers();
            const uint32_t stride = streamBuffers.empty() ? 0 : streamBuffers[0].m_byteStride;
            if (stride == 0)
            {
                continue;
            }

            RHIHandle drawEntity = rhiCtx->CreateEntity();

            DrawRequest req;
            req.m_drawInstanceArgs = RHI::DrawInstanceArguments(1, 0);
            req.m_vertexBuffer = gpu.m_vertexBuffer;
            req.m_vertexBufferInfo = VertexBufferInfo{
                0, static_cast<uint32_t>(vbByteCount), stride};

            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                auto* ibComp = rhiCtx->TryGet<RHI::Components::Buffer>(gpu.m_indexBindings);
                if (ibComp && ibComp->m_buffer)
                {
                    req.m_drawArguments = RHI::DrawArguments(
                        RHI::DrawIndexed(0, gpu.m_indexCount, 0));
                    req.m_indexBuffer = gpu.m_indexBindings;
                    req.m_indexBufferInfo = IndexBufferInfo{
                        0, static_cast<uint32_t>(ibComp->m_buffer->GetDescriptor().m_byteCount),
                        gpu.m_indexFormat};
                }
            }
            else
            {
                const uint32_t vertexCount = static_cast<uint32_t>(vbByteCount / stride);
                req.m_drawArguments = RHI::DrawArguments(
                    RHI::DrawLinear(0, vertexCount));
            }

            req.m_viewports.resize(1);
            req.m_scissors.resize(1);
            req.m_viewportsCount = 1;
            req.m_viewports[0]   = RHI::Viewport{0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y)};
            req.m_scissorsCount  = 1;
            req.m_scissors[0]    = RHI::Scissor{0, 0, renderSize.x, renderSize.y};

            // Per-draw ShaderBindings at space1: model matrix from Transform
            // system, or identity if the entity has no transform component.
            if (auto* passCtx = PassExecuteContext::Current())
            {
                auto modelHandle = CreatePassShaderBindings<SPARK_PASS_TAG("DepthPrePass")>(
                    *passCtx, *rhiCtx, /*spaceId*/ 1);

                if (modelHandle.m_bindings)
                {
                    Math::Matrix4X4 modelMatrix = Math::Matrix4X4Const::IDENTITY;
                    if (auto* worldMatrix = world->TryGet<Transform::WorldTransformMatrix>(entity))
                    {
                        modelMatrix = worldMatrix->m_worldMatrix;
                    }

                    auto* modelInput = modelHandle.m_bindings->FindConstantInput(
                        RHI::InputName("g_Model"));
                    if (modelInput)
                    {
                        modelInput->SetData(&modelMatrix, sizeof(modelMatrix));
                    }

                    m_meshToModelBinding[entity] = modelHandle.m_entity;
                    req.m_shaderBindingEntities.push_back(modelHandle.m_entity);
                }
            }

            rhiCtx->Add<DrawRequest>(drawEntity, eastl::move(req));
            rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(drawEntity);

            m_meshToDrawRequest[entity] = drawEntity;
        }

        // Prune DrawRequests and model bindings for entities that no longer
        // carry MeshGPUComponent.
        for (auto it = m_meshToDrawRequest.begin(); it != m_meshToDrawRequest.end(); )
        {
            if (!world->TryGet<Mesh::MeshGPUComponent>(it->first))
            {
                if (rhiCtx->Valid(it->second))
                {
                    rhiCtx->DestoryEntity(it->second);
                }

                auto modelIt = m_meshToModelBinding.find(it->first);
                if (modelIt != m_meshToModelBinding.end())
                {
                    if (rhiCtx->Valid(modelIt->second))
                    {
                        rhiCtx->DestoryEntity(modelIt->second);
                    }
                    m_meshToModelBinding.erase(modelIt);
                }

                it = m_meshToDrawRequest.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
