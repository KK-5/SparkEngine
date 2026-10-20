#pragma once

#include <EASTL/hash_map.h>

#include <Base.h>

#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>

namespace Spark::RHI
{
    class Device;
    class Factory;
    class PipelineLibrary;
}

namespace Spark::Render
{
    //! Draw PSOs by their description, so one description is built once. Never evicts: what
    //! it hands out lives as long as the cache.
    class DrawPipelineCache
    {
    public:
        void Init(RHI::Factory& factory, RHI::Device& device, RHI::PipelineLibrary* pipelineLibrary);

        //! The PSO of `descriptor`, built the first time it is asked for. `passName` only names
        //! the pass in asserts.
        const Ptr<RHI::PipelineState>& Acquire(const RHI::PipelineStateDescriptorForDraw& descriptor, const char* passName);

        void Shutdown();

    private:
        //! The description is kept to tell apart two that hash alike.
        struct Entry
        {
            RHI::PipelineStateDescriptorForDraw m_descriptor;
            Ptr<RHI::PipelineState>             m_pso;
        };

        RHI::Factory*         m_factory         = nullptr;
        RHI::Device*          m_device          = nullptr;
        RHI::PipelineLibrary* m_pipelineLibrary = nullptr;

        eastl::hash_multimap<size_t, Entry> m_entries;
    };
}
