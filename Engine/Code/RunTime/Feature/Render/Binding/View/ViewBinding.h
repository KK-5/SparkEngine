#pragma once

#include <Binding/GlobalBuffer.h>

#include "ViewData.h"

namespace Spark::Render
{
    //! Names the g_Views array (space1) for GlobalBuffer and its slot refs.
    struct Views {};

    //! A view's g_Views slot, on the view entity. Get() is the view index a shader reads
    //! GetView() with, and it does not move for the view's life.
    using ViewSlotRef = SlotRef<Views>;

    //! The single shared ShaderBindings entity carrying g_Views (space1).
    struct ViewBindingTag {};

    //! The view's g_Views index, for a Scope that picks its view in Build and sets
    //! .Constant("viewIndex", ...) itself. False before the view has a slot — its first
    //! frame, or while g_Views has not materialized: skip the Scope that frame.
    inline bool TryGetViewIndex(RHI::RHIContext& rhiCtx, RHI::RHIHandle view, uint32_t& outIndex)
    {
        const auto* slot = rhiCtx.TryGet<ViewSlotRef>(view);
        if (slot == nullptr)
        {
            return false;
        }
        outIndex = slot->Get();
        return true;
    }
}
