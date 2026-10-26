#include "DrawList.h"

#include <algorithm>

#include <EASTL/tuple.h>

#include <Math/Bit.h>
#include <Math/Vector4.h>

namespace Spark::Render
{
    namespace
    {
        //! What the storage is sorted by first, an instance's run in it.
        eastl::tuple<uint32_t, uint32_t> ListOf(const VisibleInstance& instance)
        {
            return eastl::make_tuple(static_cast<uint32_t>(instance.m_view), instance.m_drawMaskBitIndex);
        }

        //! Around the box's eight corners once moved by transform.
        Math::AABB TransformBounds(const Math::AABB& bounds, const Math::Matrix4X4& transform)
        {
            Math::AABB result;
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const Math::Vector3 local(
                    (corner & 1) ? bounds.max.x : bounds.min.x,
                    (corner & 2) ? bounds.max.y : bounds.min.y,
                    (corner & 4) ? bounds.max.z : bounds.min.z);
                const Math::Vector3 world(transform * Math::Vector4(local, 1.0f));
                if (corner == 0)
                {
                    result.min = world;
                    result.max = world;
                }
                else
                {
                    result.Expand(world);
                }
            }
            return result;
        }
    }

    void AddDrawListInstances(DrawContext& drawCtx, DrawMask drawMask, uint32_t position, PipelineKey key,
        const MeshGeometry& geometry, const Math::Matrix4X4& model)
    {
        const DrawMask listed = drawMask & CpuCulledDrawMask;
        if (listed == 0)
        {
            return;
        }

        DrawListInstance instance;
        instance.m_position     = position;
        instance.m_key          = key;
        instance.m_indexCount   = geometry.m_indexCount;
        instance.m_firstIndex   = geometry.m_firstIndex;
        instance.m_vertexOffset = static_cast<int32_t>(geometry.m_vertexOffset);
        instance.m_worldBounds  = TransformBounds(geometry.m_localBounds, model);

        for (uint32_t bit = 0; bit < DrawMaskBitCount; ++bit)
        {
            if (CheckBit(listed, bit))
            {
                instance.m_drawMaskBitIndex = bit;
                drawCtx.Add<DrawListInstance>(drawCtx.CreateEntity(), instance);
            }
        }
    }

    void SortVisibleInstances(DrawContext& drawCtx)
    {
        drawCtx.Sort<VisibleInstance>([](const VisibleInstance& a, const VisibleInstance& b)
        {
            // Negated so that the farther comes first.
            return eastl::make_tuple(ListOf(a), -a.m_depth, a.m_key.m_value)
                 < eastl::make_tuple(ListOf(b), -b.m_depth, b.m_key.m_value);
        });
    }

    VisibleInstanceRange VisibleInstancesOf(const DrawContext& drawCtx, RHI::RHIHandle view, uint32_t bit)
    {
        const auto* storage = drawCtx.GetStorage<VisibleInstance>();
        if (storage == nullptr)
        {
            return {};
        }

        VisibleInstance probe;
        probe.m_view             = view;
        probe.m_drawMaskBitIndex = bit;
        // std's: EASTL's algorithms do not take entt's iterators as random access.
        const auto [first, last] = std::equal_range(storage->cbegin(), storage->cend(), probe,
            [](const VisibleInstance& a, const VisibleInstance& b) { return ListOf(a) < ListOf(b); });
        return { first, last };
    }

    uint32_t FirstDrawOffset(const DrawContext& drawCtx, const VisibleInstanceRange& instances)
    {
        const auto* storage = drawCtx.GetStorage<VisibleInstance>();
        if (storage == nullptr)
        {
            return 0;
        }
        return static_cast<uint32_t>(instances.begin() - storage->cbegin());
    }
}
