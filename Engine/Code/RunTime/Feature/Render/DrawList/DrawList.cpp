#include "DrawList.h"

#include <algorithm>

#include <EASTL/tuple.h>

#include <Math/Bit.h>

namespace Spark::Render
{
    namespace
    {
        //! What the storage is sorted by first, an instance's run in it.
        eastl::tuple<uint32_t, uint32_t> ListOf(const VisibleInstance& instance)
        {
            return eastl::make_tuple(static_cast<uint32_t>(instance.m_view), instance.m_drawMaskBitIndex);
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
        instance.m_worldBounds  = Math::TransformBounds(geometry.m_localBounds, model);

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
