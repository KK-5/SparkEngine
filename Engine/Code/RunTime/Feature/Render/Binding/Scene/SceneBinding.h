#pragma once

#include <RHI/Resource/AccessFlags.h>
#include <Scene/SceneTextureComponents.h>

#include <Pass/TagList.h>
#include <Suballocation/Suballocator.h>

#include "LightData.h"

namespace Spark::Render
{
    //! Stable g_Lights slot and the last encoded record, on the world light entity.
    struct LightSlot
    {
        Suballocation m_range;
        LightRecord m_record;

        uint32_t GetIndex() const { return static_cast<uint32_t>(m_range.GetOffset() / sizeof(LightRecord)); }
    };

    //! This frame's removal update for a light slot.
    struct RemovedLightSlot
    {
        uint32_t m_index = 0;
        LightData m_previous;
    };

    //! Persistent g_Lights buffer, indexed by light slot.
    struct LightBufferTag {};

    //! Exclusive upper bound on the current light slots. The range can contain holes.
    struct SceneLightSlotCount
    {
        uint32_t m_count = 0;
    };


    struct ShadowViewBufferTag
    {
    };

    struct ViewShadowMapBufferTag
    {
    };
}
