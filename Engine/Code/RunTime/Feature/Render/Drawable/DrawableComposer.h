#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Bridges world entities and Drawable entities. Each frame: cascade-reaps
    //! Drawables whose referenced resources died, then composes new Drawables
    //! for renderable world entities not yet tagged with WorldComposedTag.
    //! Holds no internal state — the bridge is entirely in ECS components
    //! (WorldComposedTag on world, dependency refs on Drawable).
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked between
    //! InstanceBindingSystem and the Processors.
    class DrawableComposer
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
