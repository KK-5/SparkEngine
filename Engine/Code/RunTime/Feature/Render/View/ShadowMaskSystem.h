#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Hands out ShadowMask slots and owns the projection pass's draw.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced after ShadowViewSystem (it grants slots to the lights
    //! that were given atlas tiles) and before SceneBindingSystem (which marshals the slot
    //! into LightData).
    //!
    //! It writes its draw's DrawItem and pass tag itself instead of going through
    //! DrawItemRouter: the router derives once and caches, which is right for geometry that
    //! does not change, and wrong for an instance count that follows the light set. The
    //! executer joins <PassTag, DrawItem> and asks nothing about where they came from.
    class ShadowMaskSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        RHI::RHIHandle m_draw = RHI::NullHandle;
    };
}
