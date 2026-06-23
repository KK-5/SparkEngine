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

} // namespace Spark::Render

namespace Spark
{
    template<>
    struct ContextTraits<Render::Pass>
    {
        using ContextType = Render::PassContext;
    };

} // namespace Spark

namespace Spark::Render
{
    using PassExecuteContext = ExecuteContext<Pass>;
    using PassExecuteContextGuard = ExecuteContextGuard<Pass>;
}
