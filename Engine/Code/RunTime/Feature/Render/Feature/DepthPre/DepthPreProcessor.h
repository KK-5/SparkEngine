#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class PassContext;

    //! Maintains the DepthPrePass draw list. DrawRequest entities are
    //! retained 1:1 with Drawables (find-or-create + cascade reap via
    //! AssembleDrawRequests<PassTag>) — no per-frame entity churn. Each
    //! frame, Process refreshes per-draw content (view binding handle,
    //! viewport, scissor sized to renderSize) by iterating
    //! <DepthPrePassTag, DrawRequest>. The DrawRequest carries the per-draw
    //! state DrawItem requires at submit; geometry / instancing / instance
    //! binding flow through the Drawable.
    class DepthPreProcessor final
    {
    public:
        void Init();
        void Shutdown();
        void Process(const Math::Vector2Int& renderSize);
    };
}
