#pragma once

#include <ECS/EntityTraits.h>

namespace Spark::Render
{
    enum class RHIHandle : uint32_t
    {
    };
}

namespace Spark
{
    template<>
    struct EntityTraits<Render::RHIHandle>
    {
        using value_type = Render::RHIHandle;
        using entity_type = uint32_t;
        using version_type = uint16_t;
        
        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
    };
}

namespace entt
{
    template<>
    struct entt_traits<Spark::Render::RHIHandle> : basic_entt_traits<Spark::EntityTraits<Spark::Render::RHIHandle>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<Spark::Render::RHIHandle>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

namespace Spark::Render
{
    inline constexpr RHIHandle NullHandle{entt::null};
}