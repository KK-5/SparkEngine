#pragma once

#include <EASTL/fixed_vector.h>

#include <Math/Vector2.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Instance/InstanceSlot.h>

#include "Drawable.h"

namespace Spark::Render
{
    //! Append every SRG tagged BindingTag (a global singleton per tag) to out.
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

    //! Per-frame update for one pass's DrawItems: rewrite the mutable fields the submit
    //! path reads. Resolves the pass's own per-pass SRG (space2, tagged PassTag) plus the
    //! shared singletons it declares (BindingTags: view / material / scene) and the
    //! instance slot table once, then over each DrawItem rewrites m_shaderBindings (its
    //! per-draw object SRG + per-pass + shared), viewport, and resolves startInstance from
    //! its slot key. The DrawItem is read-only after this, at submit.
    //!
    //! The per-pass SRG is injected unconditionally via PassTag (not listed in .Binds):
    //! it is definitionally the pass's own, created by GetOrCreatePassShaderBindings which
    //! stamps PassTag + Components::ShaderBindings. A pass without one resolves to empty.
    template<typename PassTag, typename... BindingTags>
    void BindPassDrawItems(RHI::RHIContext& ctx, const Math::Vector2Int& renderSize)
    {
        eastl::fixed_vector<const RHI::ShaderBindings*, RHI::Limits::Pipeline::ShaderInputGroupCountMax> shared;
        ResolveSharedBinding<PassTag>(ctx, shared);
        (ResolveSharedBinding<BindingTags>(ctx, shared), ...);

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
            for (const RHI::ShaderBindings* s : shared)
            {
                item.m_shaderBindings.push_back(s);
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
