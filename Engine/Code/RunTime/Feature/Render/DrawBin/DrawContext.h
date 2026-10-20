#pragma once

#include <ECS/BasicContext.h>
#include <ECS/EntityTraits.h>
#include <ECS/ExecuteContext.h>

namespace Spark::Render
{
    //! An element of the DrawContext.
    enum class DrawEntity : uint32_t
    {
    };
}

namespace Spark
{
    template<>
    struct EntityTraits<Render::DrawEntity>
    {
        using value_type = Render::DrawEntity;
        using entity_type = uint32_t;
        using version_type = uint16_t;

        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
    };
}

namespace entt
{
    template<>
    struct entt_traits<Spark::Render::DrawEntity> : basic_entt_traits<Spark::EntityTraits<Spark::Render::DrawEntity>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<Spark::Render::DrawEntity>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

namespace Spark::Render
{
    //! This frame's draw preparation: what the passes drawing lists draw, from the encoding to
    //! the indirect draws (DrawBin.h). Everything in it lives one frame: RenderSystem empties it
    //! whole as each frame begins. Its elements are many to one with what owns them, which is a
    //! field of theirs, so a component's storage, sorted, is the array a consumer walks; it
    //! keeps its memory from frame to frame. It refers to other contexts by handle only.
    using DrawContext             = BasicContext<DrawEntity>;
    using DrawExecuteContext      = ExecuteContext<DrawEntity>;
    using DrawExecuteContextGuard = ExecuteContextGuard<DrawEntity>;
}
