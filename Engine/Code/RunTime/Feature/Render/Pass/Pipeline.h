#pragma once

#include <Object/ObjectName.h>
#include "PassContext.h"

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
        
    private:
        PassContext m_passContext;
        ObjectName  m_name;
    };
}