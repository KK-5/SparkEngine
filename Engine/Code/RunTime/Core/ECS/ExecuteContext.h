#pragma once

#include <EASTL/vector.h>

#include "BasicContext.h"
#include "ContextReference.h"

namespace Spark
{
    template<typename EntityType>
    class ExecuteContextGuard;

    //! Primary template: defaults to BasicContext<EntityType>.
    //! Specialize ContextTraits for your type when you need ExecuteContext to
    //! return a derived/specialized context class instead of the base.
    template<typename EntityType>
    struct ContextTraits
    {
        using ContextType = BasicContext<EntityType>;
    };

    template<typename EntityType>
    class ExecuteContext
    {
    public:
        using ContextType = typename ContextTraits<EntityType>::ContextType;

        static ContextType* Current() { return s_current; }

        template <typename TraitsType>
        static ContextReference<ContextType, TraitsType> CurrentReference()
        {
            return ContextReference<ContextType, TraitsType>(*s_current);
        }

        static void Push(ContextType& ctx)
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
        friend class ExecuteContextGuard<EntityType>;

        static inline ContextType* s_current = nullptr;
        static inline eastl::vector<ContextType*> s_stack{};
    };

    template<typename EntityType>
    class ExecuteContextGuard
    {
    public:
        using ContextType = typename ContextTraits<EntityType>::ContextType;

        explicit ExecuteContextGuard(ContextType& ctx)
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

} // namespace Spark
