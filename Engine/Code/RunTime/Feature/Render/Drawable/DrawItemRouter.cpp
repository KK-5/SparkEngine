#include "DrawItemRouter.h"

#include <EASTL/bonus/overloaded.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Command/DrawItem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>

#include <Instance/InstanceSlot.h>

#include "Drawable.h"
#include "DrawTag.h"
#include "DrawItemRoute.h"

namespace Spark::Render
{
    namespace
    {
        bool AnyDependencyDead(
            RHI::RHIContext&         ctx,
            const Drawable&          d,
            const InstanceSlotTable& table)
        {
            for (const auto& s : d.m_streams)
            {
                if (s.m_buffer != RHI::NullHandle && ctx.Has<DeadTag>(s.m_buffer))
                {
                    return true;
                }
            }
            if (d.m_index.m_indexBuffer != RHI::NullHandle && ctx.Has<DeadTag>(d.m_index.m_indexBuffer))
            {
                return true;
            }

            // Per-object data provisioning: each strategy reaps on its own deps.
            return eastl::visit(eastl::overloaded{
                [&](const SlotInstanceBinding& s) -> bool
                {
                    if (s.m_sharedBindings != RHI::NullHandle && ctx.Has<DeadTag>(s.m_sharedBindings))
                    {
                        return true;
                    }
                    if (s.m_idStream.m_buffer != RHI::NullHandle && ctx.Has<DeadTag>(s.m_idStream.m_buffer))
                    {
                        return true;
                    }
                    if (s.m_slotRef.m_id >= table.m_slots.size() ||
                        table.m_slots[s.m_slotRef.m_id] == UINT32_MAX)
                    {
                        return true;
                    }
                    return false;
                },
                [&](const DirectInstanceBinding& d2) -> bool
                {
                    return d2.m_bindings != RHI::NullHandle && ctx.Has<DeadTag>(d2.m_bindings);
                },
                [&](const NoInstanceBinding&) -> bool
                {
                    // No per-object dependency to watch, so never reaped on this axis.
                    return false;
                },
            }, d.m_instanceData);
        }

        //! Derivation-side readiness, keyed on the Drawable itself (not its producer):
        //! every geometry buffer view and the per-object SRG the DrawItem will bake must
        //! have materialized. Producer-agnostic — a Drawable created anywhere passes the
        //! same gate. Mirrors the deps AnyDependencyDead watches.
        bool DrawableReadyToDerive(RHI::RHIContext& ctx, const Drawable& d)
        {
            auto bufReady = [&](RHI::RHIHandle e) -> bool
            {
                auto* buf = ctx.TryGet<RHI::Components::Buffer>(e);
                return buf && buf->m_buffer;
            };
            auto srgReady = [&](RHI::RHIHandle e) -> bool
            {
                auto* sb = ctx.TryGet<RHI::Components::ShaderBindings>(e);
                return sb && sb->m_bindings;
            };

            for (const auto& s : d.m_streams)
            {
                if (s.m_buffer != RHI::NullHandle && !bufReady(s.m_buffer))
                {
                    return false;
                }
            }
            if (d.m_index.m_indexBuffer != RHI::NullHandle && !bufReady(d.m_index.m_indexBuffer))
            {
                return false;
            }

            return eastl::visit(eastl::overloaded{
                [&](const SlotInstanceBinding& s) -> bool
                {
                    if (s.m_idStream.m_buffer != RHI::NullHandle && !bufReady(s.m_idStream.m_buffer))
                    {
                        return false;
                    }
                    return srgReady(s.m_sharedBindings);
                },
                [&](const DirectInstanceBinding& x) -> bool
                {
                    return srgReady(x.m_bindings);
                },
                [&](const NoInstanceBinding&) -> bool
                {
                    return true;
                },
            }, d.m_instanceData);
        }

        //! Geometry portion of a DrawItem: draw args + resolved vertex/index views.
        //! startInstance is resolved per-frame at submit, so m_instanceOffset stays 0.
        RHI::DrawItem BuildGeometryDrawItem(RHI::RHIContext& ctx, const Drawable& d)
        {
            RHI::DrawItem item;
            item.m_drawArguments    = d.m_drawArgs;
            item.m_drawInstanceArgs = RHI::DrawInstanceArguments(d.m_instanceCount, 0);

            auto setStream = [&](const VertexStreamSpec& s)
            {
                auto* buf = ctx.TryGet<RHI::Components::Buffer>(s.m_buffer);
                if (!buf || !buf->m_buffer)
                {
                    LOG_ERROR("[DrawItemRouter] Vertex stream slot {} buffer entity {} unresolved.",
                        s.m_inputSlot, static_cast<uint32_t>(s.m_buffer));
                    return;
                }
                item.m_vertexBufferView.SetVertexInputView(
                    s.m_inputSlot,
                    RHI::VertexInputView(
                        *buf->m_buffer,
                        s.m_vertexBufferInfo.m_byteOffset,
                        s.m_vertexBufferInfo.m_byteCount,
                        s.m_vertexBufferInfo.m_byteStride));
            };
            for (const auto& s : d.m_streams)
            {
                setStream(s);
            }
            // Indexed provisioning adds its per-instance ID stream on top.
            if (const auto* slot = eastl::get_if<SlotInstanceBinding>(&d.m_instanceData))
            {
                setStream(slot->m_idStream);
            }

            if (d.m_index.m_indexBuffer != RHI::NullHandle)
            {
                if (auto* iBuf = ctx.TryGet<RHI::Components::Buffer>(d.m_index.m_indexBuffer))
                {
                    item.m_indexBufferView = RHI::IndexBufferView(
                        *iBuf->m_buffer,
                        d.m_index.m_indexInfo.m_byteOffset,
                        d.m_index.m_indexInfo.m_byteCount,
                        d.m_index.m_indexInfo.m_format);
                }
                else
                {
                    LOG_ERROR("[DrawItemRouter] Index buffer entity {} unresolved.",
                        static_cast<uint32_t>(d.m_index.m_indexBuffer));
                }
            }

            return item;
        }

        //! Per-object SRG (space4) for a Drawable: shared g_Instances for indexed
        //! provisioning, the draw's own CBV for direct, none for procedural.
        const RHI::ShaderBindings* ResolvePerObjectBindings(RHI::RHIContext& ctx, const Drawable& d)
        {
            const RHI::RHIHandle bindings = eastl::visit(eastl::overloaded{
                [](const SlotInstanceBinding& s)   -> RHI::RHIHandle { return s.m_sharedBindings; },
                [](const DirectInstanceBinding& x) -> RHI::RHIHandle { return x.m_bindings; },
                [](const NoInstanceBinding&)       -> RHI::RHIHandle { return RHI::NullHandle; },
            }, d.m_instanceData);
            if (bindings == RHI::NullHandle)
            {
                return nullptr;
            }
            auto* sb = ctx.TryGet<RHI::Components::ShaderBindings>(bindings);
            return (sb && sb->m_bindings) ? sb->m_bindings.get() : nullptr;
        }

        //! Reap the DrawItems a dying Drawable derived, via the reverse path (O(passes)).
        void ReapDerivedDrawItems(RHI::RHIContext& ctx, RHI::RHIHandle drawable)
        {
            auto* derived = ctx.TryGet<DerivedDrawItems>(drawable);
            if (!derived)
            {
                return;
            }
            for (RHI::RHIHandle item : derived->m_items)
            {
                if (item != RHI::NullHandle && !ctx.Has<DeadTag>(item))
                {
                    ctx.Add<DeadTag>(item);
                }
            }
        }
    }

    void DrawItemRouter::Init(RHI::RHIContext& /*rhiCtx*/)
    {
        // Nothing to set up — router is stateless and bridges via ECS tags.
    }

    void DrawItemRouter::Process()
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        // Cascade reap: dependency dead → reap the Drawable and the DrawItems it
        // routed out. Needs the global instance slot table for the SlotInstanceBinding
        // liveness check; if it hasn't materialized yet (warmup) there are no live
        // Drawables to reap anyway, so skip.
        RHI::RHIHandle instanceBindingEntity = RHI::NullHandle;
        rhiCtx->GetView<InstanceBindingTag>().each([&](RHI::RHIHandle e) { instanceBindingEntity = e; });
        const auto* slotTable = instanceBindingEntity != RHI::NullHandle
            ? rhiCtx->TryGet<InstanceSlotTable>(instanceBindingEntity)
            : nullptr;

        if (slotTable)
        {
            rhiCtx->GetView<DrawableTag, Drawable>(Exclude<DeadTag>).each(
                [&](RHI::RHIHandle d, const Drawable& drawable)
            {
                if (AnyDependencyDead(*rhiCtx, drawable, *slotTable))
                {
                    rhiCtx->Add<DeadTag>(d);
                    ReapDerivedDrawItems(*rhiCtx, d);
                }
            });
        }

        // Derive: DrawItem routing needs the pass context. A warmup frame without it
        // just defers derivation — Drawables stay untagged and retry next frame.
        auto* passCtx = PassExecuteContext::Current();
        if (!passCtx)
        {
            return;
        }

        rhiCtx->GetView<DrawableTag, Drawable>(Exclude<DeadTag, DrawItemsDerivedTag>).each(
            [&](RHI::RHIHandle drawable, const Drawable& composed)
        {
            // Deps (geometry buffers, per-object SRG) are created deferred; wait until
            // they materialize before resolving views. No tag yet → retried next frame.
            if (!DrawableReadyToDerive(*rhiCtx, composed))
            {
                return;
            }

            DerivedDrawItems* derived = rhiCtx->TryGet<DerivedDrawItems>(drawable);
            const SlotInstanceBinding* slot = eastl::get_if<SlotInstanceBinding>(&composed.m_instanceData);
            const RHI::ShaderBindings* perObjectBindings = ResolvePerObjectBindings(*rhiCtx, composed);

            // One DrawItem per pass that accepts this Drawable, with everything the
            // submit path needs baked on — no back-reference to the Drawable.
            passCtx->GetView<DrawItemRoute>().each([&](auto, const DrawItemRoute& route)
            {
                if (!route.m_accepts(*rhiCtx, drawable))
                {
                    return;
                }
                RHI::RHIHandle drawItem = rhiCtx->CreateEntity();
                route.m_marks(*rhiCtx, drawItem);
                rhiCtx->Add<RHI::DrawItem>(drawItem, BuildGeometryDrawItem(*rhiCtx, composed));
                if (perObjectBindings)
                {
                    rhiCtx->Add<DrawItemObjectBinding>(drawItem, DrawItemObjectBinding{ perObjectBindings });
                }
                if (slot)
                {
                    rhiCtx->Add<DrawItemInstanceSlot>(drawItem, DrawItemInstanceSlot{ slot->m_slotRef });
                }
                if (derived)
                {
                    derived->m_items.push_back(drawItem);
                }
            });

            rhiCtx->Add<DrawItemsDerivedTag>(drawable);
        });
    }

    void DrawItemRouter::Shutdown(RHI::RHIContext& rhiCtx)
    {
        // Reap every live Drawable and the DrawItems it routed out, regardless of
        // producer. MeshDrawableComposer::Shutdown separately clears its world-side
        // WorldComposedTag so a re-init re-composes from a clean slate.
        rhiCtx.GetView<DrawableTag>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle d)
        {
            rhiCtx.Add<DeadTag>(d);
            ReapDerivedDrawItems(rhiCtx, d);
        });
    }
}
