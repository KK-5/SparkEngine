#pragma once

#include <RHI/Context/RHIContext.h>
#include <RHI/Context/UniqueRHIHandle.h>

#include "View.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    //! A view instance: an entity carrying View + ViewTag. ViewBindingSystem gives it a
    //! g_Views slot and encodes its row; a pass reads it through .Binds<ViewBindingTag>()
    //! and the viewIndex the executer writes at its handle.
    template<typename ViewTag>
    RHI::UniqueRHIHandle CreateViewEntity(RHI::RHIContext& rhiCtx)
    {
        const RHI::RHIHandle view = rhiCtx.CreateEntity();
        rhiCtx.Add<ViewTag>(view);
        rhiCtx.Add<View>(view, View{});
        return RHI::UniqueRHIHandle(view);
    }
}
