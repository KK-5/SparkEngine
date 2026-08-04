#include "MeshDrawableComposer.h"

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Command/DrawItem.h>

#include <Pass/Component/RHIComponents.h>

#include <Mesh/Components.h>

#include <Instance/InstanceSlot.h>
#include <View/ViewTags.h>

#include "Drawable.h"
#include "DrawTag.h"

namespace Spark::Render
{
    namespace
    {
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
    }

    void MeshDrawableComposer::Init(RHI::RHIContext& /*rhiCtx*/)
    {
        // Nothing to set up — composer is stateless and bridges via ECS tags.
    }

    void MeshDrawableComposer::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        // Refresh global resources every frame — Drawables that referenced an
        // older revision are caught by DrawItemRouter's cascade reap and rebuilt.
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

        if (instanceBindingEntity == RHI::NullHandle
            || idBufferEntity == RHI::NullHandle
            || idBufferByteCount == 0)
        {
            // Warmup frame — globals not yet materialized.
            return;
        }

        // Find-or-create: world entities that became renderable and not yet
        // composed. Producers that invalidate downstream resources must remove
        // WorldComposedTag themselves to trigger recomposition.
        world->GetView<Mesh::MeshGPUComponent, InstanceSlotRef>(Exclude<DeadTag, WorldComposedTag>)
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
            rhiCtx->Add<Drawable>(drawable, 
                ComposePersistent(gpu, ref, instanceBindingEntity, idBufferEntity, idBufferByteCount));
            rhiCtx->Add<DerivedDrawItems>(drawable, DerivedDrawItems{});

            // DrawItem derivation is deferred to DrawItemRouter — a producer-agnostic
            // step over every DrawableTag Drawable, not just world-composed ones.
            world->Add<WorldComposedTag>(wE);
        });
    }

    void MeshDrawableComposer::Shutdown(RHI::RHIContext& /*rhiCtx*/)
    {
        // Strip WorldComposedTag so a re-init re-composes from a clean slate. The
        // live Drawables themselves are reaped by DrawItemRouter::Shutdown (they are
        // producer-agnostic), so this composer only unwinds its own world-side tag.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<WorldComposedTag>();
        }
    }
}
