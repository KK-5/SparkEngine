#include "DrawListCullingSystem.h"

#include <EASTL/algorithm.h>

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>
#include <Math/Frustum.h>
#include <Math/Vector4.h>

#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <View/View.h>
#include <View/ViewComponents.h>

#include "DrawList.h"

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t DrawArgumentsStep = 1024;
    }

    void DrawListCullingSystem::Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx)
    {
        m_lists.clear();
        for (auto [pass, drawList] : passCtx.GetView<PassDrawList>().each())
        {
            if (drawList.m_culling != DrawCulling::Cpu)
            {
                continue;
            }

            const auto* capabilities = passCtx.TryGet<PassCapabilities>(pass);
            ASSERT(capabilities && capabilities->m_collectViews,
                "[DrawListCullingSystem] Pass '{}' draws a list but declares no .RendersView<>().",
                passCtx.Get<PassName>(pass).m_name.GetCStr());
            if (!capabilities || !capabilities->m_collectViews)
            {
                continue;
            }

            m_views.clear();
            capabilities->m_collectViews(rhiCtx, m_views);
            for (RHI::RHIHandle view : m_views)
            {
                const eastl::pair<RHI::RHIHandle, uint32_t> list{ view, drawList.m_drawMaskBitIndex };
                if (eastl::find(m_lists.begin(), m_lists.end(), list) == m_lists.end())
                {
                    m_lists.push_back(list);
                }
            }
        }

        for (const auto& [view, bit] : m_lists)
        {
            const auto* frustum = rhiCtx.TryGet<ViewFrustum>(view);
            ASSERT(frustum, "[DrawListCullingSystem] A view a list the CPU culls is drawn under has no ViewFrustum.");
            if (!frustum)
            {
                continue;
            }
            const Math::Matrix4X4& worldToView = rhiCtx.Get<View>(view).m_worldToView;

            for (auto [entity, instance] : drawCtx.GetView<DrawListInstance>().each())
            {
                // The near plane is left out, as InstanceCulling.hlsl leaves it out, so a list
                // draws the same set whichever culls it.
                if (instance.m_drawMaskBitIndex != bit
                    || !frustum->m_frustum.IntersectsAABBIgnoringNear(instance.m_worldBounds))
                {
                    continue;
                }

                VisibleInstance visible;
                visible.m_view             = view;
                visible.m_drawMaskBitIndex = bit;
                visible.m_position         = instance.m_position;
                visible.m_key              = instance.m_key;
                visible.m_indexCount       = instance.m_indexCount;
                visible.m_firstIndex       = instance.m_firstIndex;
                visible.m_vertexOffset     = instance.m_vertexOffset;
                visible.m_depth            = (worldToView * Math::Vector4(instance.m_worldBounds.Center(), 1.0f)).z;
                drawCtx.Add<VisibleInstance>(drawCtx.CreateEntity(), visible);
            }
        }

        SortVisibleInstances(drawCtx);
        WriteDrawArguments(rhiCtx, drawCtx);
    }

    void DrawListCullingSystem::WriteDrawArguments(RHI::RHIContext& rhiCtx, const DrawContext& drawCtx)
    {
        // In the storage's order, so a visible instance's draw is at its place in it (FirstDrawOffset).
        m_drawArgumentsData.clear();
        if (const auto* storage = drawCtx.GetStorage<VisibleInstance>())
        {
            for (const VisibleInstance& visible : *storage)
            {
                RHI::DrawIndexedIndirectCommand command;
                command.m_indexCount    = visible.m_indexCount;
                command.m_instanceCount = 1;
                command.m_firstIndex    = visible.m_firstIndex;
                command.m_vertexOffset  = visible.m_vertexOffset;
                command.m_firstInstance = visible.m_position;
                m_drawArgumentsData.push_back(command);
            }
        }
        const uint32_t drawCount = static_cast<uint32_t>(m_drawArgumentsData.size());

        // Made before any pass draws a list, so the frame's start makes it before one imports it.
        if (m_drawArguments == RHI::NullHandle || m_drawArgumentsCapacity < drawCount)
        {
            if (m_drawArguments != RHI::NullHandle)
            {
                rhiCtx.Add<DeadTag>(m_drawArguments);
            }
            m_drawArgumentsCapacity = AlignUp(eastl::max(drawCount, 1u), DrawArgumentsStep);

            RHI::BufferDescriptor desc;
            desc.m_byteCount = static_cast<uint64_t>(m_drawArgumentsCapacity) * sizeof(RHI::DrawIndexedIndirectCommand);
            desc.m_bindFlags = RHI::BufferBindFlags::Indirect;
            m_drawArguments = RHI::CreateBuffer(rhiCtx, ObjectName("CpuCulledDrawArguments"), desc,
                RHI::HeapMemoryLevel::Host, RHI::HostMemoryAccess::Write);
            rhiCtx.Add<RHI::PerFrameTag>(m_drawArguments);
            rhiCtx.Add<CpuCulledDrawArgumentsTag>(m_drawArguments);
        }

        // With no draw, no group reads it.
        if (drawCount > 0)
        {
            rhiCtx.AddOrReplace<RHI::PendingBufferMap>(m_drawArguments, RHI::PendingBufferMap{
                m_drawArgumentsData.data(), 0, m_drawArgumentsData.size() * sizeof(RHI::DrawIndexedIndirectCommand) });
        }
    }

    void DrawListCullingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_drawArguments != RHI::NullHandle)
        {
            rhiCtx.Add<DeadTag>(m_drawArguments);
        }
        m_drawArguments         = RHI::NullHandle;
        m_drawArgumentsCapacity = 0;
        m_drawArgumentsData.clear();
    }
}
