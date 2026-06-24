#include "DrawableComposer.h"

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
            RHI::RHIHandle           drawable,
            const VertexStreams&     streams,
            const InstanceSlotTable* table)
        {
            for (const auto& s : streams.m_streams)
            {
                if (s.m_buffer != RHI::NullHandle && ctx.Has<DeadTag>(s.m_buffer))
                {
                    return true;
                }
            }
            if (const auto* ib = ctx.TryGet<IndexBufferRef>(drawable))
            {
                if (ib->m_buffer != RHI::NullHandle && ctx.Has<DeadTag>(ib->m_buffer))
                {
                    return true;
                }
            }
            if (const auto* ibd = ctx.TryGet<InstanceBindingRef>(drawable))
            {
                if (ibd->m_binding != RHI::NullHandle && ctx.Has<DeadTag>(ibd->m_binding))
                {
                    return true;
                }
            }
            if (const auto* ref = ctx.TryGet<InstanceSlotRef>(drawable))
            {
                if (ref->m_id >= table->m_slots.size() ||
                    table->m_slots[ref->m_id] == UINT32_MAX)
                {
                    return true;
                }
            }
            return false;
        }

        void ComposePersistent(
            RHI::RHIContext&              ctx,
            RHI::RHIHandle                drawable,
            const Mesh::MeshGPUComponent& gpu,
            const InstanceSlotRef&        slotRef,
            RHI::RHIHandle                instanceBindingEntity,
            RHI::RHIHandle                idBufferEntity,
            uint32_t                      idBufferBytes)
        {
            ctx.Add<DrawableTag>(drawable);

            VertexStreams streams;
            streams.m_streams.push_back(VertexStreamSpec{
                gpu.m_vertexBuffer, /*slot*/ 0, 0, gpu.m_vertexByteCount, gpu.m_vertexByteStride });
            streams.m_streams.push_back(VertexStreamSpec{
                idBufferEntity, /*slot*/ 1, 0, idBufferBytes, sizeof(uint32_t) });
            ctx.Add<VertexStreams>(drawable, eastl::move(streams));

            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                ctx.Add<IndexBufferRef>(drawable, IndexBufferRef{
                    gpu.m_indexBindings,
                    IndexBufferInfo{ 0, gpu.m_indexByteCount, gpu.m_indexFormat } });
            }

            ctx.Add<InstanceBindingRef>(drawable, InstanceBindingRef{ instanceBindingEntity });

            DrawGeomArgs args;
            if (gpu.m_indexBindings != RHI::NullHandle)
            {
                args.m_args = RHI::DrawArguments(RHI::DrawIndexed(0, gpu.m_indexCount, 0));
            }
            else
            {
                const uint32_t vertexCount = gpu.m_vertexByteStride
                    ? gpu.m_vertexByteCount / gpu.m_vertexByteStride
                    : 0;
                args.m_args = RHI::DrawArguments(RHI::DrawLinear(0, vertexCount));
            }
            ctx.Add<DrawGeomArgs>(drawable, args);

            ctx.Add<DrawInstancing>(drawable, DrawInstancing{ 1 });
            ctx.Add<InstanceSlotRef>(drawable, slotRef);
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
        rhiCtx->GetView<DrawableTag, VertexStreams>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle d, const VertexStreams& vs)
        {
            if (AnyDependencyDead(*rhiCtx, d, vs, slotTable))
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
            ComposePersistent(*rhiCtx, drawable, gpu, ref,
                              instanceBindingEntity, idBufferEntity, idBufferByteCount);
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
