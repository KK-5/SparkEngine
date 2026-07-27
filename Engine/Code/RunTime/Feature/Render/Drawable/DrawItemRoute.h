#pragma once

#include <Math/Vector2.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! Type-erased per-pass rule for deriving DrawItems from Drawables. A pass declares
    //! it via RenderPassBuilder::Accepts<DrawTags...>() at SetUp — where its PassTag and
    //! the consumed DrawTags are known — which freezes the two functions below into
    //! pointers stored on the pass entity. DrawItemRouter enumerates it at runtime.
    struct DrawItemRoute
    {
        bool (*m_accepts)(RHI::RHIContext&, RHI::RHIHandle drawable); //!< carries all consumed DrawTags?
        void (*m_marks)(RHI::RHIContext&, RHI::RHIHandle drawItem);   //!< stamp the pass's PassTag
        //! Per-frame: rewrite this pass's DrawItems' shared bindings + viewport +
        //! startInstance. Set by RenderPassBuilder::Binds; null if the pass declared none.
        void (*m_bindPass)(RHI::RHIContext&, const Math::Vector2Int& renderSize);
    };

    template<typename... DrawTags>
    bool AcceptDrawTags(RHI::RHIContext& ctx, RHI::RHIHandle drawable)
    {
        return (ctx.Has<DrawTags>(drawable) && ...);
    }

    template<typename PassTag>
    void MarkPassTag(RHI::RHIContext& ctx, RHI::RHIHandle drawItem)
    {
        ctx.Add<PassTag>(drawItem);
    }
}
