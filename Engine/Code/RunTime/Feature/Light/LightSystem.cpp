#include "LightSystem.h"

#include <cmath>

#include <EASTL/algorithm.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Service/Service.h>
#include <Math/MathUtils.h>

#include <Transform/Components.h>

#include "Components.h"

namespace Spark::Light
{
    namespace
    {
        // The light shines along its Transform's forward axis. In the world matrix the
        // forward axis is the Z basis column (column 2), matching the camera's LH
        // +Z-forward convention. Extracting it here is the ONLY spatial computation for a
        // light — the render side never sees a transform.
        Math::Vector3 ExtractForward(const Math::Matrix4X4& world)
        {
            return Math::Normalize(Math::Vector3(world[2]));
        }
        Math::Vector3 ExtractPosition(const Math::Matrix4X4& world)
        {
            return Math::Vector3(world[3]);
        }

        //! A rect's radiance is its intensity over its area, and the polygon integral loses
        //! precision on a sliver, so neither side is allowed to reach zero.
        constexpr float kRectMinSize = 0.01f;

        //! The x axis with whatever leans along forward taken out: a parent's non-uniform
        //! scale can shear the two, and the GPU record has no room for a parallelogram.
        Math::Vector3 ExtractRight(const Math::Matrix4X4& world, const Math::Vector3& forward)
        {
            const Math::Vector3 x = Math::Vector3(world[0]);
            Math::Vector3 right   = x - forward * Math::Dot(x, forward);
            if (Math::Dot(right, right) < 1e-12f)
            {
                const Math::Vector3 up = (Math::Abs(forward.y) > 0.99f)
                    ? Math::Vector3(0.0f, 0.0f, 1.0f)
                    : Math::Vector3(0.0f, 1.0f, 0.0f);
                right = Math::Cross(up, forward);
            }
            return Math::Normalize(right);
        }

        float RectHalfExtent(float authored, const Math::Vector4& axis)
        {
            return 0.5f * eastl::max(authored * Math::Length(Math::Vector3(axis)), kRectMinSize);
        }

        Math::Sphere ResolveBounds(const LightRenderData& rd)
        {
            if (rd.m_type == LightType::Point || rd.m_type == LightType::Rect)
            {
                return Math::Sphere{ rd.m_worldPosition, rd.m_range };
            }
            return Math::Sphere::FromCone(rd.m_worldPosition, rd.m_worldDirection, rd.m_range,
                Math::Acos(Math::Clamp(rd.m_cosOuter, -1.0f, 1.0f)));
        }
    }

    void LightSystem::InitInternal()
    {
        // No default light: what lights a scene is what the scene file says. An empty world
        // is dark, which is the honest picture of an empty world.
        TickBus::Handler::BusConnect();
    }

    void LightSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }

    void LightSystem::OnTick(const FrameTime& /*time*/)
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        world->GetView<LightComponent, Transform::WorldTransformMatrix>().each(
            [&](Entity entity, const LightComponent& lc, const Transform::WorldTransformMatrix& xform)
        {
            LightRenderData rd;
            rd.m_type           = lc.m_type;
            rd.m_color          = Math::Vector3(lc.m_color); // authoring color is rgba; render side is rgb
            rd.m_intensity      = lc.m_intensity;
            rd.m_range          = lc.m_range;
            rd.m_worldDirection = ExtractForward(xform.m_worldMatrix);
            rd.m_worldPosition  = ExtractPosition(xform.m_worldMatrix);
            rd.m_cosInner       = Math::Cos(Math::Radians(lc.m_innerConeDeg));
            rd.m_cosOuter       = Math::Cos(Math::Radians(lc.m_outerConeDeg));

            if (rd.m_type == LightType::Rect)
            {
                rd.m_worldRight = ExtractRight(xform.m_worldMatrix, rd.m_worldDirection);
                rd.m_halfWidth  = RectHalfExtent(lc.m_width,  xform.m_worldMatrix[0]);
                rd.m_halfHeight = RectHalfExtent(lc.m_height, xform.m_worldMatrix[1]);
                rd.m_intensity  = lc.m_intensity / (4.0f * rd.m_halfWidth * rd.m_halfHeight);
            }

            rd.m_castShadow         = lc.m_castShadow;
            rd.m_shadowBias         = lc.m_shadowBias;
            rd.m_shadowNormalOffsetTexels = lc.m_shadowNormalOffsetTexels;
            rd.m_shadowFilterWidth        = lc.m_shadowFilterWidth;
            rd.m_shadowDistance           = lc.m_shadowDistance;

            world->AddOrReplace<LightRenderData>(entity, rd);

            if (rd.m_type == LightType::Directional)
            {
                world->Remove<LightBounds>(entity);
            }
            else
            {
                world->AddOrReplace<LightBounds>(entity, LightBounds{ ResolveBounds(rd) });
            }
        });
    }
}
