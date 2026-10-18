#include "GeometryBindingSystem.h"

#include <Log/ILogSystem.h>
#include <ECS/Common.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>

namespace Spark::Render
{
    namespace
    {
        constexpr const char* GeometryBufferName = "g_Geometries";
    }

    GeometryData EncodeGeometryData(const MeshGeometry& geometry)
    {
        GeometryData data;
        data.m_firstIndex    = geometry.m_firstIndex;
        data.m_indexCount    = geometry.m_indexCount;
        data.m_vertexOffset  = static_cast<int32_t>(geometry.m_vertexOffset);
        data.m_boundsCenter  = geometry.m_localBounds.Center();
        data.m_boundsExtents = geometry.m_localBounds.Extents();
        return data;
    }

    void GeometryBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        // The layout of GeometryBindings.hlsli, declared rather than reflected (see SharedBindings.h).
        RHI::ShaderInputList inputs;
        inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
            RHI::InputName(GeometryBufferName), RHI::ShaderInputBufferAccess::Read,
            RHI::ShaderInputBufferType::Structured, 1, 0, 0, kGeometrySpaceId));

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Geometries SRV is bound by GlobalBuffer once the upload
        // buffer materializes.
        Ptr<RHI::ShaderBindings> geometryBindings =
            CreateSharedBindings(inputs, kGeometrySpaceId, "GeometryBindingSystem");
        if (!geometryBindings)
        {
            return;
        }

        m_bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindingsEntity, RHI::Components::ShaderBindings{ geometryBindings });
        rhiCtx.Add<GeometryBindingTag>(m_bindingsEntity);

        GlobalBuffer<Geometries, GeometryData, MeshGeometry>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(GeometryBufferName);
        bufferDesc.m_inputName      = RHI::InputName(GeometryBufferName);
        bufferDesc.m_bindingsEntity = m_bindingsEntity;
        m_geometries.Init(rhiCtx, bufferDesc);
    }

    void GeometryBindingSystem::Update(uint32_t frameIndex)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        m_geometries.Update(*world, *rhiCtx, frameIndex,
            [](Entity, GeometryData& out, const MeshGeometry& geometry)
        {
            out = EncodeGeometryData(geometry);
        });
    }

    void GeometryBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            m_geometries.Shutdown(*world, rhiCtx);
        }

        if (m_bindingsEntity != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindingsEntity); }

        m_bindingsEntity = RHI::NullHandle;
    }
}
