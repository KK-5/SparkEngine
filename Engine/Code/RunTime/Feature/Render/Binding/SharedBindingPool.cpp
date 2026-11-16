#include "SharedBindingPool.h"

#include <Log/ILogSystem.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Binding/SharedGroupLayouts.h>

namespace Spark::Render
{
    RHI::ShaderBindings* SharedBindingPool::Get(uint32_t spaceId)
    {
        ASSERT(spaceId != kPerPassSpaceId && spaceId != kPerScopeSpaceId,
            "space{} has no shared group: the per-pass and root-constant spaces are per pass.",
            spaceId);
        if (spaceId == kPerPassSpaceId || spaceId == kPerScopeSpaceId)
        {
            return nullptr;
        }

        for (const Ptr<RHI::ShaderBindings>& group : m_groups)
        {
            if (group->GetSpaceId() == spaceId)
            {
                return group.get();
            }
        }

        const RHI::ShaderInputList* inputs = FindSharedGroupInputs(spaceId);
        ASSERT(inputs != nullptr, "space{} has no shared group: nothing declares it.", spaceId);
        if (inputs == nullptr)
        {
            return nullptr;
        }

        Ptr<RHI::ShaderBindings> group = CreateSharedBindings(*inputs, spaceId, "SharedBindingPool");
        if (!group)
        {
            return nullptr;
        }
        m_groups.push_back(eastl::move(group));
        return m_groups.back().get();
    }

    void SharedBindingPool::Shutdown()
    {
        m_groups.clear();
    }
}
