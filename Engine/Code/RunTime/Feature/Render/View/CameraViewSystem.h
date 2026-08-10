#pragma once

#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

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
    class CameraViewSystem
    {
    public:
        void Update(const Math::Vector2Int& renderSize);
        void Shutdown(RHI::RHIContext& rhiCtx);
    };
}
