#pragma once

namespace Spark::Render
{
    //! A list of tags as a type, e.g. the tags of the resources a binding group's inputs read
    //! (`using Uses = TagList<...>` on its binding tag).
    template<typename... Tags>
    struct TagList {};
}
