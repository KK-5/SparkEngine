#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class PassContext;

    //! Builds the DepthPrePass draw list each frame. Stateless across frames:
    //! emits one transient DrawRequest per live Drawable in RHIContext, and
    //! reaps the previous frame's at Process start. The DrawRequest carries
    //! only the Drawable handle plus per-pass injection (view binding,
    //! viewport, scissor); geometry / instancing / instance binding flow
    //! through the Drawable.
    class DepthPreProcessor final
    {
    public:
        void Init(PassContext& passCtx, RHI::RHIContext& rhiCtx);
        void Shutdown(PassContext& passCtx);
        void Process(const Math::Vector2Int& renderSize);
    };
}
