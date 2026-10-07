#pragma once

#include <RHI/Context/RHIContext.h>

#include <Geometry/MeshGeometry.h>

#include "GeometryBinding.h"

namespace Spark::Render
{
    //! The g_Geometries record of a MeshGeometry.
    GeometryData EncodeGeometryData(const MeshGeometry& geometry);

    //! Owns the g_Geometries array (space6) + its ShaderBindings: one record per
    //! MeshGeometry, saying where the mesh sits in the native buffer it is drawn from.
    //!
    //! Slot allocation, encoding and upload are all GlobalBuffer's; this only supplies the
    //! space6 SRG and the encode.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after MeshGeometrySystem,
    //! which makes the MeshGeometry, and before InstanceBindingSystem, which stores the slot.
    class GeometryBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! frameIndex picks this frame's g_Geometries copy, as for InstanceBindingSystem.
        void Update(uint32_t frameIndex);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! One record per MeshGeometry, so as many as there are instances.
        static constexpr uint32_t Capacity = 65536;

        GlobalBuffer<Geometries, GeometryData, MeshGeometry> m_geometries;

        RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;  // Components::ShaderBindings — g_Geometries @ space6
    };
}
