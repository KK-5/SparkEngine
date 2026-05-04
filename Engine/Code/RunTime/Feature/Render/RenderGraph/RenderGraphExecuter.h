#pragma once

#include <Pass/PassContext.h>


namespace Spark::RHI
{
    class CommandList;
}


namespace Spark::Render
{

    class RenderGraphExecuter
    {
    public:

    private:

        void ExecutePreBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);
        
        void ExecutePostBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

    };
}