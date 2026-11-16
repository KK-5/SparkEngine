#pragma once

namespace Spark::Render
{
    //! A list of tags as a type: what a template unpacks without an argument to read them from.
    template<typename... Tags>
    struct TagList {};
}
