#pragma once

#include <RHI/Context/RHIContext.h>

#include "ViewTags.h"

namespace Spark::Render
{
    //! The main view's T (e.g. its ViewTemporalAA settings), or null when it has none. The
    //! first main view only: passes that read one view's settings assume a single main view.
    template<typename T>
    const T* FindMainViewComponent(RHI::RHIContext& ctx)
    {
        for (auto [view, component] : ctx.GetView<MainViewTag, T>().each())
        {
            return &component;
        }
        return nullptr;
    }
}
