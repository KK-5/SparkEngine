#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! View entity -> its space1 ShaderBindings entity.
    struct ViewShaderBindings
    {
        RHI::RHIHandle m_bindings = RHI::NullHandle;
    };

    //! Source -> the MainViewTag view entity it produced. Lives on the WORLD entity (a
    //! camera today), like InstanceSlotRef. Named after the view TYPE, not the source:
    //! one camera can later source several types (a planar reflection view is derived
    //! from it too). Shadows get their own ShadowViewRefs on the light — N per source.
    struct MainViewRef
    {
        RHI::RHIHandle m_view = RHI::NullHandle;
    };

    //! Which shadow atlas tile a shadow view owns. Allocated by ShadowViewSystem and STABLE
    //! across frames: LightData::m_shadowIndex addresses g_ShadowViews, so a slot derived
    //! from iteration order would re-point a light's shadow at another light's tile on any
    //! frame the light set changes. Doubles as the index the view's m_rect is derived from.
    struct ShadowViewSlot
    {
        uint32_t m_slot = 0;
    };

    //! Source -> the ShadowViewTag view entities it produced. Lives on the WORLD light
    //! entity like MainViewRef, but N per source (six for a point light's cube faces).
    //!
    //! m_index rides along because SceneBindingSystem packs g_Lights by iteration order and
    //! needs the light's g_ShadowViews slot during that same pass — reading it here keeps it
    //! out of RHIContext. Same lifetime as m_views, so one component rather than two.
    struct ShadowViewRefs
    {
        eastl::fixed_vector<RHI::RHIHandle, 6> m_views;
        int32_t                                m_index = -1;   // -1 = casts no shadow
    };
}
