#pragma once

#include <Math/Matrix4x4.h>
#include <Math/Vector2.h>
#include <Math/Vector3.h>
#include <Math/MathUtils.h>

namespace Spark::Render
{
    //! Normalized, not pixels: one view feeds passes of different extents (half-res
    //! post, a shadow atlas tile). Field order matches RHI::Viewport::GetScaled.
    struct ViewRect
    {
        float m_minX = 0.0f;
        float m_maxX = 1.0f;
        float m_minY = 0.0f;
        float m_maxY = 1.0f;
    };

    //! Per-view camera data — pure data, usable directly as an ECS component on
    //! a view entity (multi-view later) or held by a feature. It owns ONLY the
    //! camera transforms (view + projection). Per-object model matrices and the
    //! shader-binding layout are deliberately NOT the View's concern: the View
    //! produces a world->clip transform, callers decide where it goes.
    struct View
    {
        Math::Matrix4X4 m_worldToView = Math::Matrix4X4Const::IDENTITY;   // view matrix
        Math::Matrix4X4 m_viewToClip  = Math::Matrix4X4Const::IDENTITY;   // projection matrix, never jittered

        //! Sub-pixel offset in NDC applied only where the view rasterizes; culling and
        //! anything reading m_viewToClip see the unjittered projection.
        Math::Vector2 m_jitter {0.0f, 0.0f};

        //! The part of its passes' render target this view draws into, as a fraction of it.
        ViewRect m_rect {};

        //! Pixel size of that render target. Every pass rendering this view must target exactly
        //! this size, which the executer validates. Zero when the producer never said.
        Math::Vector2Int m_bufferSize {0, 0};

        //! Where this view's pixels are read from when its passes sample images of another
        //! target, e.g. an output view reading SceneColor. Zero size means the view reads its own
        //! target, so the input region is m_rect of m_bufferSize.
        ViewRect         m_inputRect {};
        Math::Vector2Int m_inputBufferSize {0, 0};

        //! Linear exposure multiplier applied before the tone curve in the tonemap pass.
        //! 1.0 = neutral (current behavior); a real EV100/auto-exposure source feeds this later.
        float m_exposure = 1.0f;

        //! Combined world->clip (projection * view). This is the value a shader's
        //! per-view constant (e.g. g_ViewProjection) should receive. Multiply by a
        //! per-object model matrix on the caller side when the shader expects a
        //! pre-combined MVP.
        Math::Matrix4X4 GetWorldToClip() const { return m_viewToClip * m_worldToView; }

        Math::Matrix4X4 GetJitteredWorldToClip() const
        {
            return Math::JitterProjection(m_viewToClip, m_jitter) * m_worldToView;
        }
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

}
