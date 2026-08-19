#pragma once

#include <Binding/GlobalBuffer.h>

#include "MaterialData.h"

namespace Spark::Render
{
    //! Names the g_Materials array (space3) for GlobalBuffer and its slot refs.
    struct Materials {};

    //! A material's reference to its g_Materials slot, on the MATERIAL entity. m_id is
    //! the GPU index directly and does not move for the material's life, so a consumer
    //! resolves it once per encode instead of reading a slot rewritten every frame.
    //! IsValid() is what tells a stale copy apart from a live one.
    using MaterialSlotRef = GlobalBufferSlotRef<Materials>;

    //! Marks the single ShaderBindings entity (in the RHIContext) that holds the
    //! g_Materials SRV at space3. A pass declares .Binds<MaterialBindingTag>() and the
    //! executer binds it once before the pass's draws, exactly like MainViewTag (space1).
    struct MaterialBindingTag {};
}
