#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class PassContext;

    //! Maintains the GBufferPass draw list, mirroring DepthPreProcessor: DrawRequest
    //! entities are retained 1:1 with Drawables (find-or-create + cascade reap via
    //! AssembleDrawRequests<GBufferPassTag>). Each frame Process injects the shared
    //! view binding (space0) by tag and sizes viewport/scissor to renderSize. The
    //! same Drawables that feed DepthPrePass feed this pass — geometry / instancing
    //! flow through the Drawable, so the two passes draw the same geometry twice
    //! (depth prepass writes depth, GBuffer runs depth-equal and writes the MRT).
    class GBufferProcessor final
    {
    public:
        void Init();
        void Shutdown();
        void Process(const Math::Vector2Int& renderSize);
    };
}
