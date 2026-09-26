#pragma once

#include <EASTL/fixed_vector.h>

#include <Math/Frustum.h>
#include <Math/Matrix4x4.h>
#include <Math/Vector2.h>
#include <RHI/Context/RHIHandle.h>

#include "ShadowPools.h"

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

    //! The previous frame's View, for views that run temporal passes. Its producer adds it
    //! invalid to opt in; ViewBindingSystem alone fills and rolls it. A view without one
    //! encodes its previous frame as the current one.
    struct ViewHistory
    {
        Math::Matrix4X4 m_worldToView = Math::Matrix4X4Const::IDENTITY;
        Math::Matrix4X4 m_viewToClip  = Math::Matrix4X4Const::IDENTITY;
        Math::Vector2   m_jitter {0.0f, 0.0f};
        bool            m_valid = false;
    };

    //! Discards the view's history this frame: a camera cut or a teleport, where last
    //! frame's matrices would read as motion.
    struct ViewHistoryResetTag {};

    //! The view runs temporal AA: its source's TemporalAAComponent, validated. Written by
    //! CameraViewSystem; TemporalAAPass and TonemapPass read it.
    struct ViewTemporalAA
    {
        float    m_currentFrameWeight = 1.0f / 16.0f;
        float    m_motionFrameWeight  = 0.25f;
        float    m_varianceClipGamma  = 1.25f;
        float    m_filterSize         = 1.0f;
        uint32_t m_jitterSamples      = 8;
    };

    //! The view runs bloom: the BloomComponent resolved for its camera (its own, else the
    //! post-process volumes'), validated; absent when that resolves to zero intensity. Written
    //! by CameraViewSystem; SceneDownsamplePass, BloomPass and TonemapPass read it.
    struct ViewBloom
    {
        float m_intensity = 0.04f;
    };

    //! Source -> the MainViewTag view entity it produced. Lives on the WORLD entity (a
    //! camera today), like InstanceSlotRef. Named after the view TYPE, not the source:
    //! one camera can later source several types (a planar reflection view is derived
    //! from it too). Shadows get their own ShadowViewRefs on the light — N per source.
    struct MainViewRef
    {
        RHI::RHIHandle m_view = RHI::NullHandle;
    };

    //! Source -> the OutputViewTag view entity it produced, beside MainViewRef.
    struct OutputViewRef
    {
        RHI::RHIHandle m_view = RHI::NullHandle;
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
    //! m_rows rides along because SceneBindingSystem packs g_Lights by iteration order and
    //! needs the light's g_ShadowViews row during that same pass — reading it here keeps it
    //! out of RHIContext. Same lifetime as m_views, so one component rather than two. It
    //! addresses the FIRST of the light's rows: a point light's six faces occupy a
    //! contiguous run, so one int still addresses them all.
    //!
    //! A view's position in m_views IS its face index, and stays so for the light's life:
    //! a face culled this frame keeps its slot, inactive and holding no tile.
    struct ShadowViewRefs
    {
        eastl::fixed_vector<RHI::RHIHandle, 6> m_views;
        ShadowViewRows                         m_rows;

        //! -1 when the light holds no rows, which is what the shader reads as "casts no
        //! shadow".
        int32_t BaseIndex() const
        {
            return m_rows.IsValid() ? static_cast<int32_t>(m_rows.Get()) : -1;
        }
    };
}
