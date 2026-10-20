#include "DrawPipelineCache.h"

#include <Log/ILogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Device/Device.h>

namespace Spark::Render
{
    void DrawPipelineCache::Init(RHI::Factory& factory, RHI::Device& device, RHI::PipelineLibrary* pipelineLibrary)
    {
        m_factory         = &factory;
        m_device          = &device;
        m_pipelineLibrary = pipelineLibrary;
    }

    const Ptr<RHI::PipelineState>& DrawPipelineCache::Acquire(const RHI::PipelineStateDescriptorForDraw& descriptor,
                                                              const char* passName)
    {
        ASSERT(m_factory && m_device, "[DrawPipelineCache] Acquire called before Init.");

        const size_t hash = descriptor.GetHash();
        const auto [first, last] = m_entries.equal_range(hash);
        for (auto it = first; it != last; ++it)
        {
            if (it->second.m_descriptor == descriptor)
            {
                return it->second.m_pso;
            }
        }

        Ptr<RHI::PipelineState> pso = m_factory->CreatePipelineState();
        const RHI::ResultCode result = pso->Init(*m_device, descriptor, m_pipelineLibrary);
        ASSERT(result == RHI::ResultCode::Success, "[DrawPipelineCache] Failed to create a PSO for pass '{}'.", passName);

        // A node of the multimap stays put, so the reference stays good.
        return m_entries.insert(eastl::make_pair(hash, Entry{ descriptor, eastl::move(pso) }))->second.m_pso;
    }

    void DrawPipelineCache::Shutdown()
    {
        m_entries.clear();
        m_factory         = nullptr;
        m_device          = nullptr;
        m_pipelineLibrary = nullptr;
    }
}
