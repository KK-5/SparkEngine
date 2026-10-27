#pragma once

#include <RHI/Context/RHIContext.h>

#include <Pass/TagList.h>

#include "ViewData.h"

namespace Spark::Render
{
    //! A view's g_Views row this frame, on the view entity: the view index a shader reads
    //! GetView() with. Rows are packed anew every frame, so it holds for this frame only;
    //! what stores it is rebuilt each frame too.
    struct ViewIndex
    {
        uint32_t m_index = 0;
    };

    //! The single shared ShaderBindings entity carrying g_Views (space1).
    struct ViewBindingTag
    {
        using Uses = TagList<>;
    };

    //! The view's g_Views index, for a Scope that picks its view in Build and sets
    //! .Constant("viewIndex", ...) itself. False when the view has no row this frame: skip
    //! the Scope.
    inline bool TryGetViewIndex(RHI::RHIContext& rhiCtx, RHI::RHIHandle view, uint32_t& outIndex)
    {
        const auto* index = rhiCtx.TryGet<ViewIndex>(view);
        if (index == nullptr)
        {
            return false;
        }
        outIndex = index->m_index;
        return true;
    }
}
