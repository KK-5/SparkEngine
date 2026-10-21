#pragma once

#include <cstdint>

#include <Math/AABB.h>
#include <Math/Matrix4x4.h>

#include <Drawable/DrawMask.h>
#include <Geometry/MeshGeometry.h>
#include <PipelineState/PipelineKey.h>
#include <PipelineState/PipelineKeyIndex.h>

#include "DrawContext.h"

namespace Spark::Render
{
    // What is known of each draw list, the instances a tag classifies, as elements of the
    // DrawContext (DrawContext.h). The encoding writes them; DrawGroupSystem makes the groups
    // (DrawGroup.h) from them.

    //! One pipeline key among the instances of a list the GPU culls: how many encoded instances
    //! carry it. The encoding publishes one a key the list has (DrawListKeyCounter), which is
    //! all the CPU knows of such a list.
    struct DrawListKey
    {
        uint32_t         m_drawMaskBitIndex = 0;
        PipelineKeyIndex m_keyIndex;
        PipelineKey      m_key;
        uint32_t         m_instanceCount = 0;
    };

    //! One encoded instance of a list the CPU culls: what culling tests it by, and what its
    //! group and its record are made from.
    struct DrawListInstance
    {
        uint32_t    m_drawMaskBitIndex = 0;
        //! Its g_Instances slot, which its record draws as its first instance.
        uint32_t    m_slot = 0;
        PipelineKey m_key;

        uint32_t m_indexCount   = 0;
        uint32_t m_firstIndex   = 0;
        int32_t  m_vertexOffset = 0;

        Math::AABB m_worldBounds;
    };

    //! A DrawListInstance of the instance for each bit of drawMask the CPU culls.
    void AddDrawListInstances(DrawContext& drawCtx, DrawMask drawMask, uint32_t slot, PipelineKey key,
        const MeshGeometry& geometry, const Math::Matrix4X4& model);
}
