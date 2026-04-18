#pragma once

#include "PassContext.h"

namespace Spark::Render
{
    class Pipeline final
    {
    public:
        void Init();

        void Shutdown();
        
    private:
        void BuildPipeline();

        void BuildUIPass();

        PassContext m_passContext;
    };
}