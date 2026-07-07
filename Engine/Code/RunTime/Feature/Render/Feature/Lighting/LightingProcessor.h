#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! Drives the deferred LightingPass each frame. Owns a procedural full-screen
    //! triangle drawable and a persistent draw-request carrier entity. Process builds
    //! the request that references the shared view SRG (space0) and this pass's GBuffer
    //! SRG (space1). The GBuffer SRG is get-or-created here (so it exists before
    //! CompileDrawRequests) but its texture slots are filled by LightingPass's Compile
    //! hook, once the GBuffer is materialized.
    class LightingProcessor
    {
    public:
        void Init();
        void Shutdown();
        void Process(const Math::Vector2Int& renderSize);

    private:
        RHI::RHIHandle m_drawRequest = RHI::NullHandle;
        RHI::RHIHandle m_drawable    = RHI::NullHandle;
    };
}
