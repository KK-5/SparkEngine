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
#include <Pass/ComputePass.h>
#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Resource/AssetManagerInterface.h>
#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Device/Device.h>
#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>
#include <RHI/RayTracing/RayTracingInstance.h>

namespace Spark::Render
{
    namespace
    {
        const RHI::AttachmentId StorageName("RayTracingSceneStorage");
        const RHI::AttachmentId ScratchName("RayTracingSceneScratch");
        const RHI::AttachmentId InstancesName("RayTracingSceneInstances");
        constexpr auto BottomLevelBuildFlags = RHI::AccelerationStructureBuildFlags::PreferFastTrace;
        constexpr auto TopLevelBuildFlags = RHI::AccelerationStructureBuildFlags::PreferFastTrace;

        RHI::RHIHandle CreateSceneBuffer(RHI::RHIContext& context, RHI::BufferPool& pool,
            const RHI::AttachmentId& name, RHI::BufferBindFlags bindFlags, uint64_t byteCount)
        {
            Ptr<RHI::Buffer> buffer = Service<RHI::Factory>::Get()->CreateBuffer();
            RHI::BufferDescriptor descriptor(bindFlags, byteCount);
            descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
            if (!buffer || pool.InitBuffer({ *buffer, descriptor }) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[RayTracingSceneSystem] Failed to create buffer {} of {} bytes.", name.GetCStr(), byteCount);
                return RHI::NullHandle;
            }
            buffer->SetName(name);

            const RHI::RHIHandle resource = context.CreateEntity();
            context.Add<RHI::ResourceName>(resource, RHI::ResourceName{ name });
            context.Add<RHI::ImportedTag>(resource);
            context.Add<RHI::Components::Buffer>(resource, RHI::Components::Buffer{ eastl::move(buffer) });
            return resource;
        }
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
        if (sizes.m_resultSize == 0 || sizes.m_buildScratchSize == 0
            || AlignUp(sizes.m_buildScratchSize, 4u) > UINT32_MAX)
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

        // AS builds use GPU addresses; transient aliasing can make resource identity ambiguous (DX12 #926).
        // Keep storage, scratch and instance buffers owned by the scene system.
        RHI::BufferPoolDescriptor poolDescriptor;
        poolDescriptor.m_heapMemoryLevel = RHI::HeapMemoryLevel::Device;
        poolDescriptor.m_bindFlags = RHI::BufferBindFlags::RayTracingAccelerationStructure
            | RHI::BufferBindFlags::RayTracingScratchBuffer
            | RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::RayTracingBuildInput;
        poolDescriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
        m_bufferPool = Service<RHI::Factory>::Get()->CreateBufferPool();
        if (!m_bufferPool || m_bufferPool->Init(device, poolDescriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RayTracingSceneSystem] Failed to initialize the buffer pool.");
            Shutdown(rhiContext);
            return;
        }
        m_bufferPool->SetName(ObjectName("RayTracingSceneBufferPool"));

        m_storageResource = CreateSceneBuffer(rhiContext, *m_bufferPool, StorageName,
            RHI::BufferBindFlags::RayTracingAccelerationStructure, StorageCapacityInBytes);
        if (m_storageResource == RHI::NullHandle
            || !EnsureScratchBuffer(rhiContext, eastl::max(ScratchBytesPerFrame, AlignUp(sizes.m_buildScratchSize, 4u))))
        {
            Shutdown(rhiContext);
            return;
        }
        m_instanceResource = CreateSceneBuffer(rhiContext, *m_bufferPool, InstancesName,
            RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::RayTracingBuildInput,
            uint64_t(RayTracingInstanceCapacity) * sizeof(RHI::RayTracingInstance));
        if (m_instanceResource == RHI::NullHandle)
        {
            Shutdown(rhiContext);
            return;
        }
        rhiContext.Add<RayTracingSceneTag>(m_storageResource);
        RayTracingScene scene;
        scene.m_topLevelView = RHI::BufferViewDescriptor::CreateRaw(
            static_cast<uint32_t>(m_topLevelAllocation.GetOffset()), static_cast<uint32_t>(m_topLevelAllocation.GetSize()));
        rhiContext.Add<RayTracingScene>(m_storageResource, scene);
    }

    bool RayTracingSceneSystem::EnsureScratchBuffer(RHI::RHIContext& rhiContext, uint64_t byteCount)
    {
        if (m_scratchResource != RHI::NullHandle
            && rhiContext.Get<RHI::Components::Buffer>(m_scratchResource).m_buffer->GetDescriptor().m_byteCount >= byteCount)
        {
            return true;
        }

        const RHI::RHIHandle resource = CreateSceneBuffer(rhiContext, *m_bufferPool, ScratchName,
            RHI::BufferBindFlags::RayTracingScratchBuffer, byteCount);
        if (resource == RHI::NullHandle)
        {
            return false;
        }
        if (m_scratchResource != RHI::NullHandle)
        {
            // A new entity also drops the old view cache; the pool defers its native allocation release.
            rhiContext.DestoryEntity(m_scratchResource);
        }
        m_scratchResource = resource;
        return true;
    }

    void RayTracingSceneSystem::SetUp(PassContext& passContext)
    {
        if (!m_allocator)
        {
            return;
        }

        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");
        const auto assetId = assetManager->MakeAssetId("engine://Shaders/RayTracing/RayTracingInstances.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[RayTracingSceneSystem] Failed to load shader RayTracingInstances.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(passContext, "RayTracingSceneInstances")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([this](ComputePassScopes& p)
            {
                const RHI::AttachmentId idsName("InstanceIDs");
                const RHI::AttachmentId instancesPoolName("Instances");
                const RHI::AttachmentId geometriesPoolName("Geometries");
                if (m_instanceCount != 0
                    && !(p.Import<InstanceIDBufferTag>(idsName)
                        && p.Import<InstancePoolTag>(instancesPoolName)
                        && p.Import<GeometryPoolTag>(geometriesPoolName)))
                {
                    m_instanceCount = 0;
                }

                p.Import(InstancesName, m_instanceResource);

                auto scope = p.Scope();
                if (m_instanceCount != 0)
                {
                    scope.ReadBuffer(idsName)
                        .View(RHI::BufferViewDescriptor::CreateStructured(0, m_instanceCount, sizeof(uint32_t)))
                        .Bind(RHI::InputName("g_InstanceIDs"));
                    scope.ReadBuffer(instancesPoolName);
                    scope.ReadBuffer(geometriesPoolName);
                }
                scope.WriteBuffer(InstancesName)
                    .View(RHI::BufferViewDescriptor::CreateStructured(
                        0, RayTracingInstanceCapacity, sizeof(RHI::RayTracingInstance)))
                    .Bind(RHI::InputName("g_RayTracingInstances"));
                scope.Constant(RHI::InputName("instanceCount"), m_instanceCount);
                scope.Dispatch(eastl::max(m_instanceCount, 1u));
                scope.Close();
            })
            .Finalize();

        SPARK_ACCELERATION_STRUCTURE_PASS(passContext, "RayTracingSceneBuild")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .Build([this](AccelerationStructurePassScopes& p)
            {
                auto& scene = RHI::RHIExecuteContext::Current()->Get<RayTracingScene>(m_storageResource);
                if (!scene.m_topLevel)
                {
                    return;
                }

                p.Import(StorageName, m_storageResource);
                p.Import(ScratchName, m_scratchResource);
                p.Import(InstancesName, m_instanceResource);
                const uint64_t topLevelScratchSize = AlignUp(m_topLevelBuildSizes.m_buildScratchSize, 4u);

                if (!m_bottomLevelBuilds.empty())
                {
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
                }

                // AS barriers cover the shared buffer: read BLAS contents and write the TLAS.
                auto scope = p.Scope();
                scope.BuildTopLevel(
                    scope.ReadWriteAccelerationStructure(StorageName).View(scene.m_topLevelView)
                        .AccelerationStructure(scene.m_topLevel),
                    scope.BuildInput(InstancesName)
                        .View(RHI::BufferViewDescriptor::CreateStructured(0, m_instanceCount, sizeof(RHI::RayTracingInstance))),
                    scope.Scratch(ScratchName)
                        .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(topLevelScratchSize))),
                    TopLevelBuildFlags);
                scope.Close();
                scene.m_ready = true;
            })
            .Finalize();
    }

    void RayTracingSceneSystem::Update()
    {
        m_bottomLevelBuilds.clear();
        m_scratchByteCount = 0;
        m_instanceCount = 0;

        RemoveStaleRayTracingBottomLevels();
        auto* world = WorldExecuteContext::Current();
        auto* rhiContext = RHI::RHIExecuteContext::Current();
        if (!world || !rhiContext || !m_allocator || !RHI::IsResourceReady(*rhiContext, m_storageResource))
        {
            return;
        }

        const auto& storageBuffer = *rhiContext->Get<RHI::Components::Buffer>(m_storageResource).m_buffer;
        RHI::Device& device = storageBuffer.GetDevice();
        auto& scene = rhiContext->Get<RayTracingScene>(m_storageResource);
        scene.m_ready = false;
        if (!scene.m_topLevel)
        {
            RHI::AccelerationStructureDescriptor descriptor;
            descriptor.m_type = RHI::AccelerationStructureType::TopLevel;
            descriptor.m_buffer = &storageBuffer;
            descriptor.m_offset = m_topLevelAllocation.GetOffset();
            descriptor.m_size = m_topLevelAllocation.GetSize();
            Ptr<RHI::AccelerationStructure> structure = Service<RHI::Factory>::Get()->CreateAccelerationStructure();
            if (!structure || structure->Init(device, descriptor) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[RayTracingSceneSystem] Failed to create the TLAS.");
                return;
            }
            scene.m_topLevel = eastl::move(structure);
        }
        static_assert(InstanceCapacity <= (1u << 24), "A ray tracing instance ID has 24 bits.");
        for (auto [entity, count] : rhiContext->GetView<ActiveInstanceCount>().each())
        {
            m_instanceCount = count.m_count;
        }
        if (m_instanceCount > RayTracingInstanceCapacity)
        {
            if (!m_instanceLimitWarned)
            {
                LOG_WARN("[RayTracingSceneSystem] {} active instances exceed the TLAS capacity {}; truncating.",
                    m_instanceCount, RayTracingInstanceCapacity);
                m_instanceLimitWarned = true;
            }
            m_instanceCount = RayTracingInstanceCapacity;
        }
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
            if (!EnsureScratchBuffer(*rhiContext, scratchEnd))
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
        m_instanceCount = 0;
        m_instanceLimitWarned = false;
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
        if (m_scratchResource != RHI::NullHandle)
        {
            rhiContext.DestoryEntity(m_scratchResource);
            m_scratchResource = RHI::NullHandle;
        }
        if (m_instanceResource != RHI::NullHandle)
        {
            rhiContext.DestoryEntity(m_instanceResource);
            m_instanceResource = RHI::NullHandle;
        }
        m_allocator.reset();
        if (m_bufferPool && m_bufferPool->IsInitialized())
        {
            // Queued BLAS objects can still hold the pool's storage buffer.
            m_bufferPool->GetDevice().FlushReleaseQueue();
        }
        m_bufferPool.reset();
    }
}
