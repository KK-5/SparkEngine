#pragma once

#include <ECS/EntityTraits.h>

namespace Spark::Material
{
    //! Strongly-typed handle for entities living in the MaterialContext. Distinct
    //! from Entity (world) and RHI::RHIHandle (RHI resources) at compile time, so a
    //! material reference is unmistakable. Backed by an entt registry, so the handle
    //! carries an id + version and is ABA-safe: a destroyed material's handle stays
    //! invalid even after its id slot is reused by a new material.
    enum class MaterialHandle : uint32_t
    {
    };
}

namespace Spark
{
    template<>
    struct EntityTraits<Material::MaterialHandle>
    {
        using value_type = Material::MaterialHandle;
        using entity_type = uint32_t;
        using version_type = uint16_t;

        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
    };
}

namespace entt
{
    template<>
    struct entt_traits<Spark::Material::MaterialHandle> : basic_entt_traits<Spark::EntityTraits<Spark::Material::MaterialHandle>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<Spark::Material::MaterialHandle>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

namespace Spark::Material
{
    inline constexpr MaterialHandle NullMaterial{entt::null};
}
