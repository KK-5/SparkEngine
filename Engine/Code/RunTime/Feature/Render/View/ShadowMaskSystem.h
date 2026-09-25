#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Hands out ShadowMask slots and records how many slices they fill (ShadowMaskLayout).
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced after ShadowViewSystem (it grants slots to the lights
    //! that were given atlas tiles) and before SceneBindingSystem (which marshals the slot
    //! into LightData).
    class ShadowMaskSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        RHI::RHIHandle m_layout = RHI::NullHandle;
    };
}
