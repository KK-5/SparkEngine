#pragma once

#include <Math/Matrix4x4.h>
#include <Math/Vector3.h>
#include <Math/MathUtils.h>

#include <Log/ILogSystem.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

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
    //! g_ViewProjection;` in the ViewBindings group at space0). This is a
    //! convention, mirroring Atom's hardcoded view-constant names.
    inline constexpr const char* ViewProjectionConstantName = "g_ViewProjection";

    //! Write the View's per-view constants into the ViewBindings group by the
    //! reserved names above. The caller owns the binding group and must call
    //! MarkShaderBindingsUpdate after all of the frame's writes are done — this
    //! only stages the data, it does not mark the group dirty.
    inline void WriteViewConstants(const View& view, RHI::ShaderBindings& srg)
    {
        const Math::Matrix4X4 worldToClip = view.GetWorldToClip();
        auto* input = srg.FindConstantInput(RHI::InputName(ViewProjectionConstantName));
        ASSERT(input, "[View] view SRG has no '{}' constant.", ViewProjectionConstantName);
        input->SetData(&worldToClip, sizeof(worldToClip));
    }
}
