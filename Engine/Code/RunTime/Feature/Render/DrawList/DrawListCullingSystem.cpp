#include "DrawListCullingSystem.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Math/Frustum.h>
#include <Math/Vector4.h>

#include <RHI/Component/Component.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <View/View.h>
#include <View/ViewComponents.h>

#include "DrawList.h"

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t InitialDrawArgumentsCapacity = 1024;
    }

    bool DrawListCullingSystem::Init(RHI::Device& device)
    {
        StagingBuffer::Descriptor descriptor;
        descriptor.m_name            = ObjectName("CpuCulledDrawArguments");
        descriptor.m_bindFlags       = RHI::BufferBindFlags::Indirect;
        descriptor.m_initialCapacity = uint64_t(InitialDrawArgumentsCapacity) * sizeof(RHI::DrawIndexedIndirectCommand);
        return m_drawArguments.Init(device, descriptor);
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
        // Import by tag must find only this frame's slot, including after a resize.
        rhiCtx.Clear<CpuCulledDrawArgumentsTag>();
        const auto* storage = drawCtx.GetStorage<VisibleInstance>();
        const uint64_t byteCount = (storage ? uint64_t(storage->size()) : 0) * sizeof(RHI::DrawIndexedIndirectCommand);
        m_drawArguments.Begin(byteCount);
        // Begin guarantees the capacity when growing the current slot succeeds.
        if (!m_drawArguments.GetBuffer())
        {
            return;
        }
        rhiCtx.Add<CpuCulledDrawArgumentsTag>(m_drawArguments.GetResource());
        // With no draw, no group reads it.
        if (byteCount == 0)
        {
            return;
        }

        // In the storage's order, so a visible instance's draw is at its place in it (FirstDrawOffset).
        auto* destination = static_cast<RHI::DrawIndexedIndirectCommand*>(m_drawArguments.Allocate(byteCount).m_data);
        for (const VisibleInstance& visible : *storage)
        {
            RHI::DrawIndexedIndirectCommand command;
            command.m_indexCount    = visible.m_indexCount;
            command.m_instanceCount = 1;
            command.m_firstIndex    = visible.m_firstIndex;
            command.m_vertexOffset  = visible.m_vertexOffset;
            command.m_firstInstance = visible.m_position;
            *destination++ = command;
        }
    }

    void DrawListCullingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.Clear<CpuCulledDrawArgumentsTag>();
        m_drawArguments.Shutdown();
    }
}
