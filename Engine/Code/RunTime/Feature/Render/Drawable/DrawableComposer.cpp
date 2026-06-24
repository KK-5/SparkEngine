#include "DrawableComposer.h"

#include <EASTL/bonus/overloaded.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>

#include <Mesh/Components.h>

#include <Instance/InstanceSlot.h>

#include "Drawable.h"

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
            if (d.m_indexBuffer != RHI::NullHandle && ctx.Has<DeadTag>(d.m_indexBuffer))
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
                gpu.m_vertexBuffer, /*slot*/ 0, 0, gpu.m_vertexByteCount, gpu.m_vertexByteStride });

            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                d.m_indexBuffer = gpu.m_indexBindings;
                d.m_indexInfo   = IndexBufferInfo{ 0, gpu.m_indexByteCount, gpu.m_indexFormat };
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
                idBufferEntity, /*slot*/ 1, 0, idBufferBytes, sizeof(uint32_t) };
            d.m_instanceData = slot;

            return d;
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

        // Cascade reap: any dependency dead → mark Drawable DeadTag.
        rhiCtx->GetView<DrawableTag, Drawable>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle d, const Drawable& drawable)
        {
            if (AnyDependencyDead(*rhiCtx, drawable, *slotTable))
            {
                rhiCtx->Add<DeadTag>(d);
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

            RHI::RHIHandle drawable = rhiCtx->CreateEntity();
            rhiCtx->Add<DrawableTag>(drawable);
            rhiCtx->Add<Drawable>(drawable, ComposePersistent(
                gpu, ref, instanceBindingEntity, idBufferEntity, idBufferByteCount));
            world->Add<WorldComposedTag>(wE);
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
            [&](RHI::RHIHandle d) { rhiCtx.Add<DeadTag>(d); });
    }
}
