#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Stages every live view's View data into its own space1 ShaderBindings — the single
    //! encoding step, deliberately blind to who produced the view. A camera view, a shadow
    //! view and a sample's hand-built view all reach the GPU through here, so a producer
    //! only ever writes the View component.
    //!
    //! Views with no ViewShaderBindings (a pass whose shader declares no space1) are skipped
    //! by the join, not by a check.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced AFTER every view producer and before the graph runs,
    //! so a view created this frame is compiled in the same frame.
    //!
    //! Unlike the other binding systems it owns no buffer of its own: a view's constants go
    //! straight into that view's own space1 group, so there is no array and no frameIndex.
    class ViewBindingSystem
    {
    public:
        void Update();
    };
}
