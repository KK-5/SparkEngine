#include "MeshGeometryComposer.h"

#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Command/DrawItem.h>

#include <Pass/Component/RHIComponents.h>

#include <Geometry/MeshGeometry.h>

#include "GeometrySpec.h"
#include "DrawMask.h"

namespace Spark::Render
{
    namespace
    {
        GeometrySpec ComposePersistent(
            const MeshGeometry& geometry,
            const InstanceSlotRef&        slotRef,
            RHI::RHIHandle                idBufferEntity,
            uint32_t                      idBufferBytes)
        {
            GeometrySpec d;
            d.m_streams.push_back(VertexStreamSpec{
                geometry.m_vertexBuffer.Get(), /*slot*/ 0, VertexBufferInfo{ 0, geometry.m_vertexByteCount, geometry.m_vertexByteStride } });

            if (geometry.m_indexBuffer.IsValid())
            {
                d.m_index.m_indexBuffer = geometry.m_indexBuffer.Get();
                d.m_index.m_indexInfo   = IndexBufferInfo{ 0, geometry.m_indexByteCount, geometry.m_indexFormat };
                d.m_drawArgs    = RHI::DrawArguments(RHI::DrawIndexed(0, geometry.m_indexCount, 0));
            }
            else
            {
                const uint32_t vertexCount = geometry.m_vertexByteStride
                    ? geometry.m_vertexByteCount / geometry.m_vertexByteStride
                    : 0;
                d.m_drawArgs = RHI::DrawArguments(RHI::DrawLinear(0, vertexCount));
            }

            d.m_instanceCount = 1;

            // Indexed provisioning: g_Instances slot + identity ID stream at slot 1
            // (per-instance, fed by StartInstanceLocation).
            SlotInstanceBinding slot;
            slot.m_slotRef  = slotRef.Weak();
            slot.m_idStream = VertexStreamSpec{
                idBufferEntity, /*slot*/ 1, VertexBufferInfo{ 0, idBufferBytes, sizeof(uint32_t) } };
            d.m_instanceData = slot;

            return d;
        }
    }

    void MeshGeometryComposer::Init(RHI::RHIContext& /*rhiCtx*/)
    {
        // Nothing to set up — composer is stateless and bridges via ECS tags.
    }

    void MeshGeometryComposer::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        // Refresh global resources every frame — specs that referenced an
        // older revision are caught by DrawItemRouter's cascade reap and rebuilt.
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

        if (idBufferEntity == RHI::NullHandle || idBufferByteCount == 0)
        {
            // Warmup frame — globals not yet materialized.
            return;
        }

        // The spec of an entity whose slot went back is reaped (DrawItemRouter), so the
        // entity composes again once it has one. Collected first: the tag is the pool
        // being iterated.
        eastl::vector<Entity> lostSlot;
        world->GetView<WorldComposedTag>(Exclude<InstanceSlotRef>).each([&](Entity wE)
        {
            lostSlot.push_back(wE);
        });
        for (Entity wE : lostSlot)
        {
            world->Remove<WorldComposedTag>(wE);
        }

        // Find-or-create: world entities that became renderable and not yet
        // composed. Producers that invalidate downstream resources must remove
        // WorldComposedTag themselves to trigger recomposition. An entity whose geometry
        // is not ready yet is picked up the frame it is.
        world->GetView<MeshGeometry, InstanceSlotRef, MeshGeometryReadyTag>(Exclude<DeadTag, WorldComposedTag>)
            .each([&](Entity wE, const MeshGeometry& geometry, const InstanceSlotRef& ref)
        {
            RHI::RHIHandle spec = rhiCtx->CreateEntity();
            AddDrawTags(*rhiCtx, spec, ClassifyDraw(*world, wE));
            rhiCtx->Add<GeometrySpec>(spec,
                ComposePersistent(geometry, ref, idBufferEntity, idBufferByteCount));

            // DrawItem derivation is deferred to DrawItemRouter — a producer-agnostic
            // step over every GeometrySpec, not just world-composed ones.
            world->Add<WorldComposedTag>(wE);
        });
    }

    void MeshGeometryComposer::Shutdown(RHI::RHIContext& /*rhiCtx*/)
    {
        // Strip WorldComposedTag so a re-init re-composes from a clean slate. The
        // live specs themselves are reaped by DrawItemRouter::Shutdown (they are
        // producer-agnostic), so this composer only unwinds its own world-side tag.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<WorldComposedTag>();
        }
    }
}
