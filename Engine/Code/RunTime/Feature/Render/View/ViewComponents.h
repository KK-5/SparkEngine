#pragma once

#include <EASTL/fixed_vector.h>

#include <Math/Frustum.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! The view's culling volume, derived from its View.
    struct ViewFrustum
    {
        Math::Frustum m_frustum;
    };

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

    //! The allocator's record of where in the atlas a shadow view rasterizes. Opaque —
    //! nothing outside ShadowViewSystem may derive anything from the value, least of all an
    //! array index. The rect it produced lives on View::m_rect.
    //!
    //! It equals ShadowViewIndex today only because both allocators are first-fit over
    //! equally sized bitsets and move in lockstep. A resolution ladder makes this an
    //! allocator node rather than a cell of a fixed grid, and the two part ways.
    struct ShadowAtlasTile
    {
        uint32_t m_tile = 0;
    };

    //! Which row of g_ShadowViews holds this view's ShadowViewData. The only number
    //! PackShadowViews may address by, and consistent within a frame: LightData::m_shadowIndex
    //! is read from the light in that same pass, so a row that moved mid-frame would point one
    //! light's shadow at another light's matrices. Stability ACROSS frames is not required —
    //! only tile caching would need that.
    struct ShadowViewIndex
    {
        uint32_t m_index = 0;
    };

    //! Source -> the ShadowViewTag view entities it produced. Lives on the WORLD light
    //! entity like MainViewRef, but N per source (six for a point light's cube faces).
    //!
    //! m_baseIndex rides along because SceneBindingSystem packs g_Lights by iteration order
    //! and needs the light's g_ShadowViews row during that same pass — reading it here keeps
    //! it out of RHIContext. Same lifetime as m_views, so one component rather than two.
    //! It is the FIRST of the light's rows: a point light's six faces will occupy a
    //! contiguous run, so one int still addresses them all.
    struct ShadowViewRefs
    {
        eastl::fixed_vector<RHI::RHIHandle, 6> m_views;
        int32_t                                m_baseIndex = -1;   // -1 = casts no shadow
    };
}
