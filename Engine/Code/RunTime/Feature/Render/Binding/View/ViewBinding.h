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
}
