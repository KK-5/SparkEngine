#pragma once

#include <EASTL/fixed_vector.h>

#include <Math/Frustum.h>
#include <Math/Matrix4x4.h>
#include <Math/Vector2.h>
#include <RHI/Context/UniqueRHIHandle.h>

#include <Suballocation/Suballocator.h>

#include "ShadowMapLayout.h"

namespace Spark::Render
{
    //! The view's culling volume, derived from its View.
    struct ViewFrustum
    {
        Math::Frustum m_frustum;
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

    //! The view gets screen-space ambient occlusion: the AmbientOcclusionComponent resolved for
    //! its camera (its own, else the post-process volumes'), validated; absent when that
    //! resolves to zero intensity. Written by CameraViewSystem; AmbientOcclusionPass and the
    //! indirect lighting passes read it (through SceneTextures).
    struct ViewAmbientOcclusion
    {
        float    m_intensity  = 1.0f;
        float    m_radius     = 0.5f;
        //! Directions the main pass integrates per pixel, from the component's quality.
        uint32_t m_sliceCount = 3;
    };

    //! The view asks for screen-space reflections: the ScreenSpaceReflectionComponent resolved
    //! for its camera (its own, else the post-process volumes'), validated; absent when that
    //! resolves to zero intensity. Written by CameraViewSystem. Whether the frame gets them is
    //! SceneTextures::ScreenSpaceReflections::FindView's to say: the view must run temporal AA
    //! as well.
    struct ViewScreenSpaceReflection
    {
        float    m_intensity    = 1.0f;
        float    m_maxRoughness = 0.4f;
        //! Steps a ray takes before it gives up, from the component's quality.
        uint32_t m_maxSteps     = 64;
    };

    //! The view is tone mapped with AgX, graded by this look: the TonemapComponent resolved for its
    //! camera (its own, else the post-process volumes'), its look turned into the parameters
    //! below. Absent when nothing sets it, and TonemapPass then uses Reinhard. Written by
    //! CameraViewSystem.
    struct ViewTonemap
    {
        //! Scales log2 exposure around middle grey, before AgX's curve.
        float m_contrast   = 1.0f;
        //! Around luma, on the same log2 encoding.
        float m_saturation = 1.0f;
        //! Blender's Greyscale look: luminance in place of color, before the log2 encoding.
        bool  m_greyscale  = false;
    };

    //! Source -> the MainViewTag view entity it produced. Lives on the WORLD entity (a
    //! camera today), like InstanceSlot. Named after the view TYPE, not the source:
    //! one camera can later source several types (a planar reflection view is derived
    //! from it too). Shadows get their own ShadowViewRefs on the light — N per source.
    //!
    //! Owns the view: the ref going, however the source went, marks it dead.
    struct MainViewRef
    {
        RHI::UniqueRHIHandle m_view;
    };

    //! Source -> the OutputViewTag view entity it produced, beside MainViewRef.
    struct OutputViewRef
    {
        RHI::UniqueRHIHandle m_view;
    };

    //! Source -> the ShadowViewTag view entities it produced. Lives on the WORLD light
    //! entity like MainViewRef, but N per source (six for a point light's cube faces).
    //!
    //! A view's position in m_views is which of the light's shadow maps it renders, a cube's
    //! face, and stays so for the light's life: a view culled this frame keeps its slot, inactive
    //! and its shadow map decommitted.
    //!
    //! Owns its views like MainViewRef. Move-only spelled out: fixed_vector's copy constructor
    //! is declared whatever its element, so without it the struct would look copyable.
    struct ShadowViewRefs
    {
        ShadowViewRefs()                            = default;
        ShadowViewRefs(ShadowViewRefs&&)            = default;
        ShadowViewRefs& operator=(ShadowViewRefs&&) = default;

        eastl::fixed_vector<RHI::UniqueRHIHandle, 6> m_views;
    };

    //! A light's shadow, on the WORLD light entity beside its ShadowViewRefs: the shadow maps
    //! its views render into, view i into First() + i, and the level they were granted. Present
    //! exactly while the light holds a shadow. A view's g_ShadowViews row is its shadow map's
    //! index too.
    struct ShadowMaps
    {
        Suballocation m_range;
        uint32_t      m_level = kNoShadowLevel;

        uint32_t First() const { return static_cast<uint32_t>(m_range.GetOffset()); }
    };
}
