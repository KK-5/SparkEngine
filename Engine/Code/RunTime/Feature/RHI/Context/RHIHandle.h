#pragma once

#include <ECS/EntityTraits.h>

namespace Spark::RHI
{
    enum class RHIHandle : uint32_t
    {
    };
}

namespace Spark
{
    template<>
    struct EntityTraits<RHI::RHIHandle>
    {
        using value_type = RHI::RHIHandle;
        using entity_type = uint32_t;
        using version_type = uint16_t;

        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
    };
}

namespace entt
{
    template<>
    struct entt_traits<Spark::RHI::RHIHandle> : basic_entt_traits<Spark::EntityTraits<Spark::RHI::RHIHandle>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<Spark::RHI::RHIHandle>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

namespace Spark::RHI
{
    inline constexpr RHIHandle NullHandle{entt::null};
}
