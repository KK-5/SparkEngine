#pragma once

#include "PassContext.h"
#include "RHIContext.h"

namespace Spark::Render
{
    class Pipeline final
    {
    public:
        void Init();

        void Shutdown();

        PassContext& GetContext();
        
    private:
        void BuildPipeline();

        void BuildUIPass();

        struct RHIResources
        {
            
        };

        PassContext m_passContext;
        RHIContext  m_rhiContext;
    };
}