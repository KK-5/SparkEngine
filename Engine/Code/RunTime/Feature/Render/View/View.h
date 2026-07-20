#pragma once

#include <Math/Matrix4x4.h>
#include <Math/Vector3.h>
#include <Math/MathUtils.h>

#include <Log/ILogSystem.h>
#include <Shader/ShaderBindingsUtils.h>

namespace Spark::Render
{
    //! Per-view camera data — pure data, usable directly as an ECS component on
    //! a view entity (multi-view later) or held by a feature. It owns ONLY the
    //! camera transforms (view + projection). Per-object model matrices and the
    //! shader-binding layout are deliberately NOT the View's concern: the View
    //! produces a world->clip transform, callers decide where it goes.
    struct View
    {
        Math::Matrix4X4 m_worldToView = Math::Matrix4X4Const::IDENTITY;   // view matrix
        Math::Matrix4X4 m_viewToClip  = Math::Matrix4X4Const::IDENTITY;   // projection matrix

        //! Combined world->clip (projection * view). This is the value a shader's
        //! per-view constant (e.g. g_ViewProjection) should receive. Multiply by a
        //! per-object model matrix on the caller side when the shader expects a
        //! pre-combined MVP.
        Math::Matrix4X4 GetWorldToClip() const { return m_viewToClip * m_worldToView; }
    };

    //! Build a perspective View from camera parameters. fovYRadians is the
    //! vertical field of view in radians (use Math::Radians(deg) to convert).
    inline View MakePerspectiveView(
        const Math::Vector3& eye,
        const Math::Vector3& target,
        const Math::Vector3& up,
        float                fovYRadians,
        float                aspect,
        float                nearZ,
        float                farZ)
    {
        View view;
        view.m_worldToView = Math::LookAt(eye, target, up);
        view.m_viewToClip  = Math::PerspectiveFov(fovYRadians, aspect, nearZ, farZ);
        return view;
    }

    //! Reserved engine name for the per-view world->clip constant. Shaders pull
    //! it in via `#include "ViewBindings.hlsl"` (declares `float4x4
    //! g_ViewProjection;` in the ViewBindings group at space1). This is a
    //! convention, mirroring Atom's hardcoded view-constant names.
    inline constexpr const char* ViewProjectionConstantName    = "g_ViewProjection";
    inline constexpr const char* InvViewProjectionConstantName = "g_InvViewProj";
    inline constexpr const char* ViewConstantName              = "g_View";
    inline constexpr const char* InvViewConstantName           = "g_InvView";

    //! Write the View's per-view constants into the ViewBindings group on the given
    //! ShaderBindings ENTITY: world->clip (g_ViewProjection) and world->view (g_View),
    //! plus their inverses g_InvViewProj (clip->world) and g_InvView (view->world; its
    //! translation is the camera world position). The inverses are computed once here so
    //! consumers never re-read the camera. Built on the generic SetShaderConstant helper,
    //! so it both stages the data and marks the binding dirty for the next compile.
    inline void WriteViewConstants(const View& view, RHI::RHIHandle viewBindings)
    {
        const Math::Matrix4X4 worldToClip = view.GetWorldToClip();
        const Math::Matrix4X4 worldToView = view.m_worldToView;
        SetShaderConstant(viewBindings, RHI::InputName(ViewProjectionConstantName),    worldToClip);
        SetShaderConstant(viewBindings, RHI::InputName(InvViewProjectionConstantName), Math::Inverse(worldToClip));
        SetShaderConstant(viewBindings, RHI::InputName(ViewConstantName),              worldToView);
        SetShaderConstant(viewBindings, RHI::InputName(InvViewConstantName),           Math::Inverse(worldToView));
    }
}
