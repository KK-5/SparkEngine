#pragma once

#include <Log/SpdLogSystem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>

namespace Spark::Render
{
    class RenderGraphCompiler
    {
    public:
        
    private:
        friend class RenderGraph;

        void TopoSort();

    };
}