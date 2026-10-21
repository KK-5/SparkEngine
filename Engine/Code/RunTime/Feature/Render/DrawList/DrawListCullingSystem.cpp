#include "DrawListCullingSystem.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Math/Frustum.h>
#include <Math/Vector4.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <View/View.h>
#include <View/ViewComponents.h>

#include "DrawList.h"

namespace Spark::Render
{
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
                visible.m_slot             = instance.m_slot;
                visible.m_key              = instance.m_key;
                visible.m_indexCount       = instance.m_indexCount;
                visible.m_firstIndex       = instance.m_firstIndex;
                visible.m_vertexOffset     = instance.m_vertexOffset;
                visible.m_depth            = (worldToView * Math::Vector4(instance.m_worldBounds.Center(), 1.0f)).z;
                drawCtx.Add<VisibleInstance>(drawCtx.CreateEntity(), visible);
            }
        }

        SortVisibleInstances(drawCtx);
    }
}
