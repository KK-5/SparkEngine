#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <Log/SpdLogSystem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>

namespace Spark::RHI
{
    class TransientResourcePool;
}

namespace Spark::Render
{

    using QueueBasedPasses = eastl::array<eastl::vector<Pass>, static_cast<size_t>(RHI::HardwareQueueClass::Count)>;

    class RenderGraphCompiler
    {
    public:

    private:
        friend class RenderGraph;

        QueueBasedPasses CompilePassCrossQueue(eastl::span<Pass> passes);

        //! Allocate transient images/buffers from the pool, materialize their
        //! views, and write the backing pointers / view handles back onto the
        //! resource and attachment entities in RHIContext. Caller must ensure
        //! the pool's batch is open (post-OnFrameBegin, pre-seal).
        void CompileTransientResources(
            eastl::span<Pass>           passes,
            RHI::TransientResourcePool& pool);

        //! Per-pass aliasing barriers from the pool. Caller must ensure the
        //! pool's batch is sealed (GetAliasingBarriers requires this — see
        //! TransientResourcePool::GetAliasingBarriers docstring).
        void CompileTransientAliasingBarriers(
            eastl::span<Pass>           passes,
            RHI::TransientResourcePool& pool);
    };
}