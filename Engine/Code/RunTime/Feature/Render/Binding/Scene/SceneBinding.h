#pragma once

#include <RHI/Resource/AccessFlags.h>
#include <Resource/AssetTypes.h>
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

        uint32_t Get() const { return static_cast<uint32_t>(m_range.GetOffset() / sizeof(LightRecord)); }
    };

    //! A mesh emitter has its own light slot even when its entity also carries an analytic light.
    struct EmissiveLightSlot
    {
        LightSlot m_slot;
        Resource::AssetId m_emissiveTexture;
        bool m_activeThisFrame = false;
    };

    //! This frame's removal update for the entity's analytic light.
    struct RemovedLightSlot
    {
        uint32_t m_index = 0;
        LightData m_previous;
    };

    //! Separate component so the same entity can retire both of its light slots in one update.
    struct RemovedEmissiveLightSlot
    {
        RemovedLightSlot m_slot;
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
