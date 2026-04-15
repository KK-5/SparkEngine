#pragma once

#include <ECS/EntityTraits.h>

namespace Spark::Render
{
    enum class Pass : uint32_t
    {
    };
}

namespace Spark
{
    template<>
    struct EntityTraits<Render::Pass>
    {
        using value_type = Render::Pass;
        using entity_type = uint32_t;
        using version_type = uint16_t;
        
        static constexpr entity_type entity_mask = 0xFF;
        static constexpr entity_type version_mask = 0;
    };
}

namespace entt
{
    template<>
    struct entt_traits<Spark::Render::Pass> : basic_entt_traits<Spark::EntityTraits<Spark::Render::Pass>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<Spark::Render::Pass>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

namespace Spark::Render
{
    inline constexpr Pass NullPass{entt::null};
}