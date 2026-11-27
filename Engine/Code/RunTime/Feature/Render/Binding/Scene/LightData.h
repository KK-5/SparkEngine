#pragma once

#include <cstddef>
#include <cstdint>

#include <EASTL/array.h>
#include <Math/Vector3.h>

namespace Spark::Render
{
    inline constexpr uint32_t InvalidLightType = 0;
    inline constexpr uint32_t EmissiveMeshLightType = 5;

    //! Per-frame light parameters. LightData.hlsli decodes the type-specific data.
    struct LightData
    {
        Math::Vector3 m_color{0.0f, 0.0f, 0.0f};
        uint32_t m_type = InvalidLightType;
        eastl::array<eastl::array<uint32_t, 4>, 4> m_typeData{};
        eastl::array<uint32_t, 3> m_typeDataTail{};
        float m_intensity = 0.0f;
    };

    static_assert(sizeof(LightData) == 96 && offsetof(LightData, m_type) == 12
        && offsetof(LightData, m_typeData) == 16 && offsetof(LightData, m_typeDataTail) == 80
        && offsetof(LightData, m_intensity) == 92);

    //! One persistent light slot in g_Lights, with independent presence in each frame.
    struct LightRecord
    {
        LightData m_current;
        LightData m_previous;
    };

    static_assert(sizeof(LightRecord) == 2 * sizeof(LightData));
}
