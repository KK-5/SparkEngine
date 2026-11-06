#include "RayTracingBottomLevel.h"

#include <EASTL/vector.h>
#include <ECS/WorldContext.h>

namespace Spark::Render
{
    namespace
    {
        void ReleaseStructure(Ptr<RHI::AccelerationStructure>& structure)
        {
            if (structure)
            {
                structure->GetDevice().QueueForRelease(structure);
                structure.reset();
            }
        }
    }

    RayTracingBottomLevel& RayTracingBottomLevel::operator=(RayTracingBottomLevel&& other) noexcept
    {
        if (this != &other)
        {
            ReleaseStructure(m_structure);
            m_bufferResource = other.m_bufferResource;
            m_storage = eastl::move(other.m_storage);
            m_input = other.m_input;
            m_structure = eastl::move(other.m_structure);
        }
        return *this;
    }

    RayTracingBottomLevel::~RayTracingBottomLevel()
    {
        ReleaseStructure(m_structure);
    }

    void RemoveStaleRayTracingBottomLevels()
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        eastl::vector<Entity> stale;
        world->GetView<RayTracingBottomLevel>().each([&](Entity entity, const RayTracingBottomLevel& bottomLevel)
        {
            const auto* input = world->TryGet<RayTracingGeometryBuildInput>(entity);
            if (input == nullptr || bottomLevel.m_input != *input)
            {
                stale.push_back(entity);
            }
        });
        for (Entity entity : stale)
        {
            world->Remove<RayTracingBottomLevel>(entity);
        }
    }
}
