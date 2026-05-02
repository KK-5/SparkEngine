#pragma once

#include <EASTL/span.h>
#include <Log/SpdLogSystem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>

namespace Spark::Render
{

    using QueueBasedPasses = eastl::array<eastl::vector<Pass>, static_cast<size_t>(RHI::HardwareQueueClass::Count)>;

    class RenderGraphCompiler
    {
    public:
        
    private:
        friend class RenderGraph;
        
        QueueBasedPasses CompilePassCrossQueue(eastl::span<Pass> passes);

    };
}