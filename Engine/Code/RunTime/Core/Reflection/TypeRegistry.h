#pragma once
#include <EASTL/functional.h>
#include <EASTL/vector.h>

#include "ReflectContext.h"

namespace Spark
{
    class TypeRegistry
    {
    public:
        using RegisterFunc = eastl::function<void(ReflectContext&)>;

        static ReflectContext& GetContext()
        {
            return m_reflectContext;
        }

        static void Register(RegisterFunc func)
        {
            m_funcs.emplace_back(func);
        }

        static void RegisterAll()
        {
            for (RegisterFunc& func: m_funcs)
            {
                func(m_reflectContext);
            }
        }

    private:
        static eastl::vector<RegisterFunc> m_funcs;
        static ReflectContext m_reflectContext;
    };
}