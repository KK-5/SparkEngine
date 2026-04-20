#pragma once

#include <Object/ObjectName.h>
#include "PassContext.h"
#include "RHIContext.h"

namespace Spark::Render
{
    class Pipeline final
    {
    public:
        Pipeline() = default;
        Pipeline(eastl::string_view name);
        
        void SetName(eastl::string_view name);
        ObjectName GetName() const;

        PassContext& GetPassContext();
        const PassContext& GetPassContext() const;
        RHIContext& GetRHIContext();
        const RHIContext& GetRHIContext() const;
        
    private:
        PassContext m_passContext;
        RHIContext  m_rhiContext;
        ObjectName  m_name;
    };
}