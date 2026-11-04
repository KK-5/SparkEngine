#pragma once

#include <EASTL/vector.h>

#include <Pass/PassContext.h>
#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>
#include <RHI/RayTracing/AccelerationStructure.h>
#include <Suballocation/Suballocator.h>

#include "RayTracingGeometryBuildInput.h"

namespace Spark::Render
{
    //! Owns shared AS storage and prepares the frame's builds from world geometry inputs.
    class RayTracingSceneSystem
    {
    public:
        void Init(RHI::RHIContext& rhiContext);
        void SetUp(PassContext& passContext);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiContext);

    private:
        static constexpr uint64_t StorageCapacityInBytes = 256ull * 1024 * 1024;
        static constexpr uint64_t ScratchBytesPerFrame = 8ull * 1024 * 1024;

        struct BottomLevelBuild
        {
            RayTracingGeometryBuildInput m_input;
            RHI::BufferViewDescriptor m_storageView;
            RHI::BufferViewDescriptor m_scratchView;
            Ptr<RHI::AccelerationStructure> m_structure;
        };

        RHI::RHIHandle m_storageResource = RHI::NullHandle;
        Ptr<Suballocator> m_allocator;
        Suballocation m_topLevelAllocation;
        RHI::AccelerationStructureBuildSizes m_topLevelBuildSizes;

        eastl::vector<BottomLevelBuild> m_bottomLevelBuilds;
        uint64_t m_scratchByteCount = 0;
    };
}
