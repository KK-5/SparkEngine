#include "DrawItemRouter.h"

#include <EASTL/bonus/overloaded.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Command/DrawItem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <Pass/PassCapabilities.h>

#include "GeometrySpec.h"
#include "DrawTag.h"

namespace Spark::Render
{
    namespace
    {
        bool AnyDependencyDead(RHI::RHIContext& ctx, const GeometrySpec& d)
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
                    // The ID stream and the g_Instances bindings entity are created and
                    // reaped together by InstanceBindingSystem, so this witnesses both.
                    if (s.m_idStream.m_buffer != RHI::NullHandle && ctx.Has<DeadTag>(s.m_idStream.m_buffer))
                    {
                        return true;
                    }
                    // The slot ref carries its own occupancy version, so this catches both
                    // "the renderable died" and "that index belongs to someone else now"
                    // without the router knowing where instance slots come from.
                    return !s.m_slotRef.IsValid();
                },
                [&](const NoInstanceBinding&) -> bool
                {
                    // No per-object dependency to watch, so never reaped on this axis.
                    return false;
                },
            }, d.m_instanceData);
        }

        //! Derivation-side readiness, keyed on the spec itself (not its producer): every
        //! geometry buffer view the DrawItem will bake must have materialized.
        //! Producer-agnostic — a GeometrySpec created anywhere passes the same gate.
        //! Mirrors the deps AnyDependencyDead watches.
        bool GeometryReadyToDerive(RHI::RHIContext& ctx, const GeometrySpec& d)
        {
            auto bufReady = [&](RHI::RHIHandle e) -> bool
            {
                auto* buf = ctx.TryGet<RHI::Components::Buffer>(e);
                return buf && buf->m_buffer;
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
                    return s.m_idStream.m_buffer == RHI::NullHandle || bufReady(s.m_idStream.m_buffer);
                },
                [&](const NoInstanceBinding&) -> bool
                {
                    return true;
                },
            }, d.m_instanceData);
        }

        //! Geometry portion of a DrawItem: draw args + resolved vertex/index views.
        //! startInstance is baked here, not refreshed per frame: the slot does not move
        //! for the renderable's life, and if it stops being the renderable's slot at all
        //! the spec is reaped (AnyDependencyDead) along with this DrawItem.
        RHI::DrawItem BuildGeometryDrawItem(RHI::RHIContext& ctx, const GeometrySpec& d)
        {
            const uint32_t startInstance = eastl::visit(eastl::overloaded{
                [](const SlotInstanceBinding& s)   -> uint32_t { return s.m_slotRef.m_id; },
                [](const NoInstanceBinding&)       -> uint32_t { return 0u; },
            }, d.m_instanceData);

            RHI::DrawItem item;
            item.m_drawArguments    = d.m_drawArgs;
            item.m_drawInstanceArgs = RHI::DrawInstanceArguments(d.m_instanceCount, startInstance);

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

        // Cascade reap: dependency dead → reap the entity, which carries the DrawItem
        // too. Every dependency is self-describing (resource entities carry DeadTag, the
        // instance slot ref carries its own version), so this needs no handle to any
        // producer.
        rhiCtx->GetView<GeometrySpec>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle e, const GeometrySpec& spec)
        {
            if (AnyDependencyDead(*rhiCtx, spec))
            {
                rhiCtx->Add<DeadTag>(e);
            }
        });

        // Derive: DrawItem routing needs the pass context. A warmup frame without it
        // just defers derivation — specs stay underived and retry next frame.
        auto* passCtx = PassExecuteContext::Current();
        if (!passCtx)
        {
            return;
        }

        // Excluding DrawItem is the idempotency filter: it lands on the same entity, so
        // its presence means this spec is already derived.
        rhiCtx->GetView<GeometrySpec>(Exclude<DeadTag, RHI::DrawItem>).each(
            [&](RHI::RHIHandle e, const GeometrySpec& spec)
        {
            // Geometry buffers are created deferred; wait until they materialize before
            // resolving views. No DrawItem yet → retried next frame.
            if (!GeometryReadyToDerive(*rhiCtx, spec))
            {
                return;
            }

            // One DrawItem for the object, not one per pass: BuildGeometryDrawItem takes
            // no pass parameter, so every accepting pass would get the same bytes.
            // Unconditional, so a spec no pass accepts still counts as derived.
            rhiCtx->Add<RHI::DrawItem>(e, BuildGeometryDrawItem(*rhiCtx, spec));

            passCtx->GetView<PassCapabilities>().each([&](Pass, const PassCapabilities& caps)
            {
                if (caps.m_accepts(*rhiCtx, e))
                {
                    caps.m_markSubmitItem(*rhiCtx, e);
                }
            });
        });
    }

    void DrawItemRouter::Shutdown(RHI::RHIContext& rhiCtx)
    {
        // Reap every live spec regardless of producer. MeshGeometryComposer::Shutdown
        // separately clears its world-side WorldComposedTag so a re-init re-composes from
        // a clean slate.
        rhiCtx.GetView<GeometrySpec>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle d, const GeometrySpec&) { rhiCtx.Add<DeadTag>(d); });
    }
}
