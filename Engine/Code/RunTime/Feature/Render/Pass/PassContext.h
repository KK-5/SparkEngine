#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <ECS/BasicContext.h>
#include <ECS/ExecuteContext.h>

#include "Pass.h"

namespace Spark::Render
{
    //! ECS context for declared render passes. Inherits BasicContext<Pass> for
    //! the registry and adds a parallel declaration-order list so RenderGraph
    //! can iterate user-declared passes in the order their SPARK_RENDER_PASS
    //! callsites ran, regardless of entt's pool order.
    //!
    //! Use CreatePass for user-declared passes (the path the PassBuilder
    //! Finalize methods take). The inherited CreateEntity / DestoryEntity
    //! remain available for internally synthesized pass entities (e.g.
    //! compiler-generated sink passes) that must NOT participate in
    //! declaration-order iteration.
    class PassContext : public BasicContext<Pass>
    {
    public:
        Pass CreatePass()
        {
            Pass pass = CreateEntity();
            m_declOrder.push_back(pass);
            return pass;
        }

        eastl::span<const Pass> GetPassesInDeclOrder() const
        {
            return { m_declOrder.data(), m_declOrder.size() };
        }

    private:
        eastl::vector<Pass> m_declOrder;
    };

    //! Thin wrapper around ExecuteContext<Pass> that exposes the stored context
    //! as PassContext* (the actual derived type). All Push sites accept
    //! PassContext& so the downcast in Current() is always safe.
    class PassExecuteContext
    {
    public:
        static PassContext* Current()
        {
            return static_cast<PassContext*>(ExecuteContext<Pass>::Current());
        }

        static void Push(PassContext& ctx) { ExecuteContext<Pass>::Push(ctx); }
        static void Pop()                  { ExecuteContext<Pass>::Pop(); }
    };

    class PassExecuteContextGuard
    {
    public:
        explicit PassExecuteContextGuard(PassContext& ctx)
        {
            PassExecuteContext::Push(ctx);
        }

        ~PassExecuteContextGuard()
        {
            PassExecuteContext::Pop();
        }

        PassExecuteContextGuard(const PassExecuteContextGuard&) = delete;
        PassExecuteContextGuard& operator=(const PassExecuteContextGuard&) = delete;
    };
}
