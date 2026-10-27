#pragma once

#include <ECS/WorldContext.h>
#include <RHI/Context/RHIContext.h>

#include <Binding/DeviceArray.h>
#include <Geometry/MeshGeometry.h>
#include <Pass/PassContext.h>

#include "GeometryBinding.h"

namespace Spark::Render
{
    //! The g_Geometries record of a MeshGeometry.
    GeometryData EncodeGeometryData(const MeshGeometry& geometry);

    //! Owns the g_Geometries array (space6) + its ShaderBindings: one record per
    //! MeshGeometry, saying where the mesh sits in the native buffer it is drawn from.
    //!
    //! A MeshGeometry's slot is a GeometrySlot, an element of g_Geometries, a DeviceArray,
    //! which writes the records that changed: a mesh's, once.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after MeshGeometrySystem,
    //! which makes the MeshGeometry, and before InstanceBindingSystem, which stores the slot.
    class GeometryBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Registers the pass writing the records, ahead of every pass that reads them.
        void SetUp(PassContext& passContext);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! One record per MeshGeometry, so as many as there are instances.
        static constexpr uint32_t Capacity = 1u << 20;

        //! A slot for each MeshGeometry; an entity that lost its MeshGeometry gives its back.
        void AllocateSlots(WorldContext& world);

        RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;  // Components::ShaderBindings — g_Geometries @ space6

        //! g_Geometries, carrying GeometryPoolTag.
        DeviceArray m_geometries;
    };
}
