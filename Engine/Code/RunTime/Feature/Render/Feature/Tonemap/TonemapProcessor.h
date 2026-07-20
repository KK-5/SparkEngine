#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! Drives the final TonemapPass each frame. Owns a procedural full-screen triangle
    //! drawable and a persistent draw-request carrier entity. Process builds the request
    //! that references this pass's SceneColor SRG (space0); the SRG's texture slot is
    //! filled by TonemapPass's Compile hook once SceneColor is materialized. Unlike the
    //! lighting/skybox processors this pass needs no per-view SRG — it only samples
    //! SceneColor by pixel, no camera matrices.
    class TonemapProcessor
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
