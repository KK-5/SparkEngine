#include "DrawableComposer.h"

#include <EASTL/bonus/overloaded.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Command/DrawItem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>

#include <Mesh/Components.h>

#include <Instance/InstanceSlot.h>
#include <View/ViewTags.h>

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

        Drawable ComposePersistent(
            const Mesh::MeshGPUComponent& gpu,
            const InstanceSlotRef&        slotRef,
            RHI::RHIHandle                instanceBindingEntity,
            RHI::RHIHandle                idBufferEntity,
            uint32_t                      idBufferBytes)
        {
            Drawable d;
            d.m_streams.push_back(VertexStreamSpec{
                gpu.m_vertexBuffer, /*slot*/ 0, VertexBufferInfo{ 0, gpu.m_vertexByteCount, gpu.m_vertexByteStride } });

            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                d.m_index.m_indexBuffer = gpu.m_indexBindings;
                d.m_index.m_indexInfo   = IndexBufferInfo{ 0, gpu.m_indexByteCount, gpu.m_indexFormat };
                d.m_drawArgs    = RHI::DrawArguments(RHI::DrawIndexed(0, gpu.m_indexCount, 0));
            }
            else
            {
                const uint32_t vertexCount = gpu.m_vertexByteStride
                    ? gpu.m_vertexByteCount / gpu.m_vertexByteStride
                    : 0;
                d.m_drawArgs = RHI::DrawArguments(RHI::DrawLinear(0, vertexCount));
            }

            d.m_instanceCount = 1;

            // Indexed provisioning: shared g_Instances SRG + slot + identity ID
            // stream at slot 1 (per-instance, fed by StartInstanceLocation).
            SlotInstanceBinding slot;
            slot.m_sharedBindings = instanceBindingEntity;
            slot.m_slotRef        = slotRef;
            slot.m_idStream       = VertexStreamSpec{
                idBufferEntity, /*slot*/ 1, VertexBufferInfo{ 0, idBufferBytes, sizeof(uint32_t) } };
            d.m_instanceData = slot;

            return d;
        }

        //! Geometry buffers are created deferred (PendingBufferInit -> OnFrameBegin), so
        //! compose must wait until they materialize before it can resolve their views.
        bool GeometryBuffersReady(RHI::RHIContext& ctx, const Mesh::MeshGPUComponent& gpu)
        {
            auto ready = [&](RHI::RHIHandle e) -> bool
            {
                auto* buf = ctx.TryGet<RHI::Components::Buffer>(e);
                return buf && buf->m_buffer;
            };
            if (!ready(gpu.m_vertexBuffer))
            {
                return false;
            }
            if (gpu.m_indexBindings != RHI::NullHandle && !ready(gpu.m_indexBindings))
            {
                return false;
            }
            return true;
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
                    LOG_ERROR("[DrawableComposer] Vertex stream slot {} buffer entity {} unresolved.",
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
                    LOG_ERROR("[DrawableComposer] Index buffer entity {} unresolved.",
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

    void DrawableComposer::Init(RHI::RHIContext& /*rhiCtx*/)
    {
        // Nothing to set up — composer is stateless and bridges via ECS tags.
    }

    void DrawableComposer::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        // Refresh global resources every frame — Drawables that referenced an
        // older revision are caught by AnyDependencyDead and rebuilt below.
        RHI::RHIHandle instanceBindingEntity = RHI::NullHandle;
        rhiCtx->GetView<InstanceBindingTag>().each(
            [&](RHI::RHIHandle e) { instanceBindingEntity = e; });

        RHI::RHIHandle idBufferEntity    = RHI::NullHandle;
        uint32_t       idBufferByteCount = 0;
        rhiCtx->GetView<InstanceIDBufferTag, RHI::Components::Buffer>().each(
            [&](RHI::RHIHandle e, const RHI::Components::Buffer& buf)
        {
            idBufferEntity    = e;
            idBufferByteCount = buf.m_buffer
                ? static_cast<uint32_t>(buf.m_buffer->GetDescriptor().m_byteCount)
                : 0;
        });

        const auto* slotTable = instanceBindingEntity != RHI::NullHandle
            ? rhiCtx->TryGet<InstanceSlotTable>(instanceBindingEntity)
            : nullptr;

        if (instanceBindingEntity == RHI::NullHandle
            || idBufferEntity == RHI::NullHandle
            || idBufferByteCount == 0
            || !slotTable)
        {
            // Warmup frame — globals not yet materialized.
            return;
        }

        // Cascade reap: dependency dead → reap the Drawable and the DrawItems it derived.
        rhiCtx->GetView<DrawableTag, Drawable>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle d, const Drawable& drawable)
        {
            if (AnyDependencyDead(*rhiCtx, drawable, *slotTable))
            {
                rhiCtx->Add<DeadTag>(d);
                ReapDerivedDrawItems(*rhiCtx, d);
            }
        });

        // Find-or-create: world entities that became renderable and not yet
        // composed. Producers that invalidate downstream resources must remove
        // WorldComposedTag themselves to trigger recomposition.
        world->GetView<Mesh::MeshGPUComponent, InstanceSlotRef>(
                  Exclude<DeadTag, WorldComposedTag>)
            .each([&](Entity wE, const Mesh::MeshGPUComponent& gpu, const InstanceSlotRef& ref)
        {
            if (gpu.m_vertexBuffer == RHI::NullHandle)
            {
                return;
            }

            // Defer compose until geometry buffers materialize; retry next frame
            // (WorldComposedTag stays unset).
            if (!GeometryBuffersReady(*rhiCtx, gpu))
            {
                return;
            }

            // Static-import barrier registration lives HERE (moved off MeshSystem to
            // sever the feature→SparkRender reverse dependency): render registers the
            // VB/IB upload→InputAssembly attachment at the point it actually consumes
            // the buffers. This find-or-create block is one-time per mesh
            // (WorldComposedTag gate), so the attachment is registered exactly once —
            // and only when the mesh becomes drawable, i.e. when the buffers are used.
            // Slot name is unused by the static-barrier path, so the resource's own
            // ResourceName stands in.
            CreateStaticBufferAttachment(*rhiCtx, gpu.m_vertexBuffer,
                rhiCtx->Get<RHI::ResourceName>(gpu.m_vertexBuffer).m_name,
                RHI::AttachmentAccess::Read,
                RHI::AttachmentUsage::InputAssembly,
                RHI::AttachmentStage::VertexInput);
            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                CreateStaticBufferAttachment(*rhiCtx, gpu.m_indexBindings,
                    rhiCtx->Get<RHI::ResourceName>(gpu.m_indexBindings).m_name,
                    RHI::AttachmentAccess::Read,
                    RHI::AttachmentUsage::InputAssembly,
                    RHI::AttachmentStage::VertexInput);
            }

            RHI::RHIHandle drawable = rhiCtx->CreateEntity();
            rhiCtx->Add<DrawableTag>(drawable);
            // Placeholder culling stamp: no culling system yet, so every composed
            // Drawable is visible to the main view. When culling lands it takes over
            // the per-view set/clear of Visible<V> and this line goes away.
            rhiCtx->Add<Visible<MainViewTag>>(drawable);
            // Everything is opaque today; becomes a per-AlphaMode split later.
            rhiCtx->Add<OpaqueTag>(drawable);
            rhiCtx->Add<Drawable>(drawable, ComposePersistent(
                gpu, ref, instanceBindingEntity, idBufferEntity, idBufferByteCount));
            rhiCtx->Add<DerivedDrawItems>(drawable, DerivedDrawItems{});

            // DrawItem derivation is deferred to DeriveDrawItems() — a producer-agnostic
            // step over every DrawableTag Drawable, not just world-composed ones.
            world->Add<WorldComposedTag>(wE);
        });
    }

    void DrawableComposer::DeriveDrawItems()
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        if (!rhiCtx || !passCtx)
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
            const SlotInstanceBinding* slot =
                eastl::get_if<SlotInstanceBinding>(&composed.m_instanceData);
            const RHI::ShaderBindings* perObjectBindings =
                ResolvePerObjectBindings(*rhiCtx, composed);

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
                // Placeholder visibility — culling will own this add/remove later.
                rhiCtx->Add<Visible<MainViewTag>>(drawItem);
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

    void DrawableComposer::Shutdown(RHI::RHIContext& rhiCtx)
    {
        // Strip WorldComposedTag so a re-init re-composes from a clean slate;
        // any live Drawables become orphans and are reaped via cascade as
        // their referenced global resources die.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<WorldComposedTag>();
        }
        rhiCtx.GetView<DrawableTag>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle d)
        {
            rhiCtx.Add<DeadTag>(d);
            ReapDerivedDrawItems(rhiCtx, d);
        });
    }
}
