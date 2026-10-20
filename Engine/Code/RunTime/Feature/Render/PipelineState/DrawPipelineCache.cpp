#include "DrawPipelineCache.h"

#include <Log/ILogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Device/Device.h>

namespace Spark::Render
{
    const RHI::PipelineState* DrawPipelineCache::Acquire(const RHI::PipelineStateDescriptorForDraw& descriptor,
                                                         RHI::Factory& factory, RHI::Device& device, const char* passName)
    {
        const size_t hash = descriptor.GetHash();
        const auto [first, last] = m_entries.equal_range(hash);
        for (auto it = first; it != last; ++it)
        {
            if (it->second.m_descriptor == descriptor)
            {
                return it->second.m_pso.get();
            }
        }

        Ptr<RHI::PipelineState> pso = factory.CreatePipelineState();
        const RHI::ResultCode result = pso->Init(device, descriptor, nullptr);
        ASSERT(result == RHI::ResultCode::Success, "[DrawPipelineCache] Failed to create a PSO for pass '{}'.", passName);

        const RHI::PipelineState* acquired = pso.get();
        m_entries.insert(eastl::make_pair(hash, Entry{ descriptor, eastl::move(pso) }));
        return acquired;
    }

    void DrawPipelineCache::Shutdown()
    {
        m_entries.clear();
    }
}
