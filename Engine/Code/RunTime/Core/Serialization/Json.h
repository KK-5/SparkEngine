#pragma once

#include <nlohmann/json_fwd.hpp>

namespace Spark
{
    //! Insertion-ordered: fields land in the file in the order they were written, so
    //! serialized assets diff cleanly. The sorted `nlohmann::json` would reorder them.
    //!
    //! This header forward-declares only. Anything that touches a JsonValue's members
    //! must include <nlohmann/json.hpp> -- from a .cpp, never from a public header.
    using JsonValue = nlohmann::ordered_json;
}
