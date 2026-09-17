#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>
#include <Tick/FrameTime.h>

namespace Spark::Render
{
    //! A view PRODUCER: reconciles MainViewTag view entities against the world's cameras
    //! every frame — find-or-create per camera, refresh its View, reap the orphans. Writes
    //! only the View component; encoding it into the view's SRG is ViewBindingSystem's job,
    //! which does that for every view regardless of who produced it.
    //!
    //! A ShadowViewSystem (lights -> N views each) would sit beside this one, not inside it.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced before the encoding step.
    //!
    //! Camera views run temporal passes, so each opts into a ViewHistory and, with jitter
    //! enabled, takes one sub-pixel offset per frame.
    class CameraViewSystem
    {
    public:
        void Update(const Math::Vector2Int& renderSize, const FrameTime& time, bool jitterEnabled);
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
