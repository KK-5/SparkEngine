#pragma once

#include <EASTL/fixed_vector.h>

#include <Math/Vector2.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Instance/InstanceSlot.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/PassComponents.h>

#include "Drawable.h"

namespace Spark::Render
{
    //! Append every ShaderBindings tagged BindingTag (a global singleton per tag) to out.
    template<typename BindingTag, size_t N>
    void ResolveSharedBinding(
        RHI::RHIContext& ctx, eastl::fixed_vector<const RHI::ShaderBindings*, N>& out)
    {
        for (auto [entity, comp] : ctx.GetView<BindingTag, RHI::Components::ShaderBindings>().each())
        {
            if (comp.m_bindings)
            {
                out.push_back(comp.m_bindings.get());
            }
        }
    }

    //! Resolve onto the pass entity what the executer binds once per pass: the pass's own
    //! per-pass bindings (space2, tagged PassTag) and the shared ones it declared.
    //!
    //! The per-pass group is injected unconditionally via PassTag (not listed in .Binds):
    //! it is definitionally the pass's own, created by GetOrCreatePassShaderBindings which
    //! stamps PassTag + Components::ShaderBindings. A pass without one resolves to empty.
    template<typename PassTag, typename... BindingTags>
    void ResolvePassSharedBindings(RHI::RHIContext& ctx)
    {
        auto* passCtx = PassExecuteContext::Current();
        if (!passCtx)
        {
            return;
        }

        PassSharedBindings shared;
        ResolveSharedBinding<PassTag>(ctx, shared.m_bindings);
        (ResolveSharedBinding<BindingTags>(ctx, shared.m_bindings), ...);
        passCtx->AddOrReplace<PassSharedBindings>(FindPass<PassTag>(*passCtx), eastl::move(shared));
    }

    //! Per-frame update for one pass's DrawItems: rewrite the mutable fields the submit
    //! path reads — its own per-object bindings, viewport, and startInstance resolved from
    //! its slot key. The DrawItem is read-only after this, at submit.
    template<typename PassTag, typename... BindingTags>
    void BindPassDrawItems(RHI::RHIContext& ctx, const Math::Vector2Int& renderSize)
    {
        ResolvePassSharedBindings<PassTag, BindingTags...>(ctx);

        const InstanceSlotTable* slotTable = nullptr;
        for (auto [entity, table] : ctx.GetView<InstanceBindingTag, InstanceSlotTable>().each())
        {
            slotTable = &table;
        }

        ctx.GetView<PassTag, RHI::DrawItem>().each([&](RHI::RHIHandle e, RHI::DrawItem& item)
        {
            item.m_shaderBindings.clear();
            if (auto* obj = ctx.TryGet<DrawItemObjectBinding>(e); obj && obj->m_objShaderBindings)
            {
                item.m_shaderBindings.push_back(obj->m_objShaderBindings);
            }
            item.m_shaderBindingsCount = static_cast<uint8_t>(item.m_shaderBindings.size());

            item.m_viewports.resize(1);
            item.m_viewports[0] = RHI::Viewport{
                0.f, static_cast<float>(renderSize.x), 0.f, static_cast<float>(renderSize.y) };
            item.m_viewportsCount = 1;
            item.m_scissors.resize(1);
            item.m_scissors[0] = RHI::Scissor{ 0, 0, renderSize.x, renderSize.y };
            item.m_scissorsCount = 1;

            if (auto* slot = ctx.TryGet<DrawItemInstanceSlot>(e); slot && slotTable)
            {
                const uint32_t id  = slot->m_slotRef.m_id;
                const uint32_t val = (id < slotTable->m_slots.size()) ? slotTable->m_slots[id] : UINT32_MAX;
                item.m_drawInstanceArgs.m_instanceOffset = (val == UINT32_MAX) ? 0u : val;
            }
        });
    }
}
