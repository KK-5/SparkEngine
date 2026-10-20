#pragma once

#include <EASTL/hash_map.h>

#include <Base.h>

#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>

namespace Spark::RHI
{
    class Device;
    class Factory;
}

namespace Spark::Render
{
    //! Draw PSOs by their description, so one description is built once. Never evicts: what
    //! it hands out lives as long as the cache.
    class DrawPipelineCache
    {
    public:
        //! The PSO of `descriptor`, built the first time it is asked for. `passName` only names
        //! the pass in asserts.
        const RHI::PipelineState* Acquire(const RHI::PipelineStateDescriptorForDraw& descriptor,
                                          RHI::Factory& factory, RHI::Device& device, const char* passName);

        void Shutdown();

    private:
        //! The description is kept to tell apart two that hash alike.
        struct Entry
        {
            RHI::PipelineStateDescriptorForDraw m_descriptor;
            Ptr<RHI::PipelineState>             m_pso;
        };

        eastl::hash_multimap<size_t, Entry> m_entries;
    };
}
