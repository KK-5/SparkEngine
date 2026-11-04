#include "RayTracingSceneSystem.h"
#include "RayTracingBottomLevel.h"
#include "RayTracingScene.h"

#include <EASTL/string.h>

#include <CoreComponents/Tags.h>
#include <ECS/WorldContext.h>
#include <Log/ILogSystem.h>
#include <Math/Bit.h>
#include <Service/Service.h>

#include <Pass/AccelerationStructurePass.h>
#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Device/Device.h>
#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>

namespace Spark::Render
{
    namespace
    {
        const RHI::AttachmentId StorageName("RayTracingSceneStorage");
        const RHI::AttachmentId ScratchName("RayTracingSceneScratch");
        constexpr auto BottomLevelBuildFlags = RHI::AccelerationStructureBuildFlags::PreferFastTrace;
        constexpr auto TopLevelBuildFlags = RHI::AccelerationStructureBuildFlags::PreferFastTrace;
    }

    void RayTracingSceneSystem::Init(RHI::RHIContext& rhiContext)
    {
        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        if (!device.GetFeatures().m_rayTracing)
        {
            return;
        }

        RHI::AccelerationStructureBuildInputs topLevelInputs;
        topLevelInputs.m_type = RHI::AccelerationStructureType::TopLevel;
        topLevelInputs.m_flags = TopLevelBuildFlags;
        topLevelInputs.m_instanceCount = RayTracingInstanceCapacity;
        const auto sizes = device.GetAccelerationStructureBuildSizes(topLevelInputs);
        if (sizes.m_resultSize == 0 || sizes.m_buildScratchSize == 0)
        {
            LOG_ERROR("[RayTracingSceneSystem] Failed to query TLAS sizes.");
            return;
        }

        m_allocator = new Suballocator(device, StorageCapacityInBytes);
        m_topLevelAllocation = m_allocator->Allocate(
            AlignUp(sizes.m_resultSize, RHI::Alignment::AccelerationStructure), RHI::Alignment::AccelerationStructure);
        if (!m_topLevelAllocation.IsValid())
        {
            LOG_ERROR("[RayTracingSceneSystem] AS storage has no room for the TLAS.");
            m_allocator.reset();
            return;
        }
        m_topLevelBuildSizes = sizes;

        RHI::BufferDescriptor descriptor(RHI::BufferBindFlags::RayTracingAccelerationStructure, StorageCapacityInBytes);
        descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
        m_storageResource = RHI::CreateBuffer(rhiContext, StorageName, descriptor);
        rhiContext.Add<RHI::ImportedTag>(m_storageResource);
        rhiContext.Add<RayTracingSceneTag>(m_storageResource);
        RayTracingScene scene;
        scene.m_topLevelView = RHI::BufferViewDescriptor::CreateRaw(
            static_cast<uint32_t>(m_topLevelAllocation.GetOffset()), static_cast<uint32_t>(m_topLevelAllocation.GetSize()));
        rhiContext.Add<RayTracingScene>(m_storageResource, scene);
    }

    void RayTracingSceneSystem::SetUp(PassContext& passContext)
    {
        SPARK_ACCELERATION_STRUCTURE_PASS(passContext, "RayTracingSceneBuild")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .Build([this](AccelerationStructurePassScopes& p)
            {
                if (m_bottomLevelBuilds.empty())
                {
                    return;
                }

                p.Import(StorageName, m_storageResource);
                RHI::BufferDescriptor scratchDescriptor(RHI::BufferBindFlags::RayTracingScratchBuffer, m_scratchByteCount);
                scratchDescriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
                p.CreateBuffer(ScratchName, scratchDescriptor);

                auto scope = p.Scope();
                auto buildInput = [&](RHI::RHIHandle resource, const RHI::BufferViewDescriptor& view)
                {
                    eastl::string name;
                    name.sprintf("RayTracingSceneInput_%u", static_cast<uint32_t>(resource));
                    const RHI::AttachmentId id(name);
                    p.Import(id, resource);
                    return scope.BuildInput(id).View(view);
                };

                for (const BottomLevelBuild& build : m_bottomLevelBuilds)
                {
                    TriangleBuildInput geometry{ buildInput(build.m_input.m_vertexBuffer, build.m_input.m_vertexView) };
                    geometry.m_vertexFormat = build.m_input.m_vertexFormat;
                    geometry.m_opaque = build.m_input.m_opaque;
                    if (build.m_input.m_indexBuffer != RHI::NullHandle)
                    {
                        geometry.m_indices = buildInput(build.m_input.m_indexBuffer, build.m_input.m_indexView);
                    }

                    scope.BuildBottomLevel(
                        scope.WriteAccelerationStructure(StorageName).View(build.m_storageView)
                            .AccelerationStructure(build.m_structure),
                        { &geometry, 1 },
                        scope.Scratch(ScratchName).View(build.m_scratchView), BottomLevelBuildFlags);
                }
                scope.Close();
            })
            .Finalize();
    }

    void RayTracingSceneSystem::Update()
    {
        m_bottomLevelBuilds.clear();
        m_scratchByteCount = 0;

        RemoveStaleRayTracingBottomLevels();
        auto* world = WorldExecuteContext::Current();
        auto* rhiContext = RHI::RHIExecuteContext::Current();
        if (!world || !rhiContext || !m_allocator || !RHI::IsResourceReady(*rhiContext, m_storageResource))
        {
            return;
        }

        const auto& storageBuffer = *rhiContext->Get<RHI::Components::Buffer>(m_storageResource).m_buffer;
        RHI::Device& device = storageBuffer.GetDevice();
        const uint32_t scratchAlignment = eastl::max(device.GetLimits().m_accelerationStructureScratchOffsetAlignment, 4u);

        world->GetView<RayTracingGeometryBuildInput>(Exclude<RayTracingBottomLevel, DeadTag>).each(
            [&](Entity entity, const RayTracingGeometryBuildInput& input)
        {
            if (m_scratchByteCount >= ScratchBytesPerFrame
                || !RHI::IsResourceReady(*rhiContext, input.m_vertexBuffer)
                || (input.m_indexBuffer != RHI::NullHandle && !RHI::IsResourceReady(*rhiContext, input.m_indexBuffer)))
            {
                return;
            }

            RHI::RayTracingGeometry geometry;
            geometry.m_vertexFormat = input.m_vertexFormat;
            geometry.m_vertexCount = input.m_vertexView.m_elementCount;
            geometry.m_vertexStride = input.m_vertexView.m_elementSize;
            geometry.m_opaque = input.m_opaque;
            if (input.m_indexBuffer != RHI::NullHandle)
            {
                const auto format = input.m_indexView.m_elementFormat;
                ASSERT(format == RHI::Format::R16_UINT || format == RHI::Format::R32_UINT,
                    "[RayTracingSceneSystem] A BLAS index view must use R16_UINT or R32_UINT.");
                geometry.m_indexFormat = format == RHI::Format::R16_UINT ? RHI::IndexFormat::UINT16 : RHI::IndexFormat::UINT32;
                geometry.m_indexCount = input.m_indexView.m_elementCount;
            }

            RHI::AccelerationStructureBuildInputs buildInputs;
            buildInputs.m_type = RHI::AccelerationStructureType::BottomLevel;
            buildInputs.m_flags = BottomLevelBuildFlags;
            buildInputs.m_geometries = { &geometry, 1 };
            const auto sizes = device.GetAccelerationStructureBuildSizes(buildInputs);
            if (sizes.m_resultSize == 0 || sizes.m_buildScratchSize == 0)
            {
                LOG_ERROR("[RayTracingSceneSystem] Failed to query BLAS sizes for entity {}.", static_cast<uint32_t>(entity));
                return;
            }

            const uint64_t scratchOffset = AlignUp(m_scratchByteCount, scratchAlignment);
            const uint64_t scratchSize = AlignUp(sizes.m_buildScratchSize, 4u);
            const uint64_t scratchEnd = scratchOffset + scratchSize;
            if (scratchEnd > UINT32_MAX)
            {
                LOG_ERROR("[RayTracingSceneSystem] BLAS scratch exceeds the supported buffer view size.");
                return;
            }
            // A build larger than the budget runs alone so it cannot wait forever.
            if (scratchEnd > ScratchBytesPerFrame && !m_bottomLevelBuilds.empty())
            {
                return;
            }

            const uint64_t resultSize = AlignUp(sizes.m_resultSize, RHI::Alignment::AccelerationStructure);
            Suballocation allocation = m_allocator->Allocate(resultSize, RHI::Alignment::AccelerationStructure);
            if (!allocation.IsValid())
            {
                LOG_ERROR("[RayTracingSceneSystem] AS storage has no room for {} bytes of BLAS.", resultSize);
                return;
            }

            RHI::AccelerationStructureDescriptor descriptor;
            descriptor.m_type = RHI::AccelerationStructureType::BottomLevel;
            descriptor.m_buffer = &storageBuffer;
            descriptor.m_offset = allocation.GetOffset();
            descriptor.m_size = allocation.GetSize();
            Ptr<RHI::AccelerationStructure> structure = Service<RHI::Factory>::Get()->CreateAccelerationStructure();
            if (!structure || structure->Init(device, descriptor) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[RayTracingSceneSystem] Failed to create BLAS for entity {}.", static_cast<uint32_t>(entity));
                return;
            }

            BottomLevelBuild build;
            build.m_input = input;
            build.m_storageView = RHI::BufferViewDescriptor::CreateRaw(
                static_cast<uint32_t>(allocation.GetOffset()), static_cast<uint32_t>(allocation.GetSize()));
            build.m_scratchView = RHI::BufferViewDescriptor::CreateRaw(
                static_cast<uint32_t>(scratchOffset), static_cast<uint32_t>(scratchSize));
            build.m_structure = structure;
            m_bottomLevelBuilds.push_back(build);
            m_scratchByteCount = scratchEnd;

            RayTracingBottomLevel bottomLevel;
            bottomLevel.m_bufferResource = m_storageResource;
            bottomLevel.m_storage = eastl::move(allocation);
            bottomLevel.m_input = input;
            bottomLevel.m_structure = eastl::move(structure);
            world->Add<RayTracingBottomLevel>(entity, eastl::move(bottomLevel));
        });
    }

    void RayTracingSceneSystem::Shutdown(RHI::RHIContext& rhiContext)
    {
        m_bottomLevelBuilds.clear();
        m_scratchByteCount = 0;
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<RayTracingBottomLevel>();
        }
        m_topLevelAllocation.Reset();
        m_topLevelBuildSizes = {};
        if (m_storageResource != RHI::NullHandle)
        {
            rhiContext.DestoryEntity(m_storageResource);
            m_storageResource = RHI::NullHandle;
        }
        m_allocator.reset();
    }
}
