#pragma once

#include <EASTL/vector.h>

#include "BasicContext.h"
#include "ContextReference.h"

namespace Spark
{
    template<typename EntityType>
    class ExecuteContextGuard;

    template<typename EntityType>
    class ExecuteContext
    {
    public:
        static BasicContext<EntityType>* Current() { return s_current; }

        template <typename TraitsType>
        static ContextReference<BasicContext<EntityType>, TraitsType> CurrentReference()
        {
            return ContextReference<BasicContext<EntityType>, TraitsType>(*s_current);
        }

        static void Push(BasicContext<EntityType>& ctx)
        {
            s_stack.push_back(s_current);
            s_current = &ctx;
        }

        static void Pop()
        {
            s_current = s_stack.back();
            s_stack.pop_back();
        }

    private:
        static inline BasicContext<EntityType>* s_current = nullptr;
        static inline eastl::vector<BasicContext<EntityType>*> s_stack{};
    };

    template<typename EntityType>
    class ExecuteContextGuard
    {
    public:
        explicit ExecuteContextGuard(BasicContext<EntityType>& ctx)
        {
            ExecuteContext<EntityType>::Push(ctx);
        }

        ~ExecuteContextGuard()
        {
            ExecuteContext<EntityType>::Pop();
        }

        ExecuteContextGuard(const ExecuteContextGuard&) = delete;
        ExecuteContextGuard& operator=(const ExecuteContextGuard&) = delete;
    };
}
