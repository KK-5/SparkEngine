#pragma once

#include <EASTL/vector.h>

#include <Pass/PassContext.h>
#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>
#include <RHI/RayTracing/AccelerationStructure.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <Suballocation/Suballocator.h>

#include "RayTracingGeometryBuildInput.h"

namespace Spark::Render
{
    //! Owns scene build buffers and prepares the frame's builds from world geometry inputs.
    class RayTracingSceneSystem
    {
    public:
        void Init(RHI::RHIContext& rhiContext);
        void SetUp(PassContext& passContext);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiContext);

    private:
        bool EnsureScratchBuffer(RHI::RHIContext& rhiContext, uint64_t byteCount);

        static constexpr uint64_t StorageCapacityInBytes = 256ull * 1024 * 1024;
        static constexpr uint64_t ScratchBytesPerFrame = 8ull * 1024 * 1024;

        struct BottomLevelBuild
        {
            RayTracingGeometryBuildInput m_input;
            RHI::BufferViewDescriptor m_storageView;
            RHI::BufferViewDescriptor m_scratchView;
            Ptr<RHI::AccelerationStructure> m_structure;
        };

        Ptr<RHI::BufferPool> m_bufferPool;
        RHI::RHIHandle m_storageResource = RHI::NullHandle;
        RHI::RHIHandle m_scratchResource = RHI::NullHandle;
        RHI::RHIHandle m_instanceResource = RHI::NullHandle;
        Ptr<Suballocator> m_allocator;
        Suballocation m_topLevelAllocation;
        RHI::AccelerationStructureBuildSizes m_topLevelBuildSizes;

        eastl::vector<BottomLevelBuild> m_bottomLevelBuilds;
        uint64_t m_scratchByteCount = 0;
        uint32_t m_instanceCount = 0;
        bool m_instanceLimitWarned = false;
    };
}
