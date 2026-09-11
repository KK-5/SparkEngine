#pragma once

#include <cstdint>

#include <entt/entt.hpp>

#include "Json.h"

namespace Spark
{
    namespace SerializeDetail
    {
        //! The encoding itself, shared by every context's handle type so the two cannot
        //! drift into two on-disk forms. Lives in a .cpp because a JsonValue's members are
        //! out of reach from a header (see Json.h).
        bool HandleToJson(uint32_t raw, bool isNull, JsonValue& out);
        bool HandleFromJson(const JsonValue& in, uint32_t& raw, bool& isNull);
    }

    //! An entity identifier's JsonOperation, for the handle type of any context -- Entity
    //! registers it from Core, MaterialHandle from SparkMaterial. Needed because these are
    //! enum classes with no enumerators: the built-in enum branch would look for a name and
    //! fail, and the lookup that finds this runs ahead of it.
    //!
    //! The identifier is written raw, with the null one as JSON null -- "unset", the same
    //! spelling every other unset field uses. Whether a non-null identifier still names a
    //! live entity is NOT asked here: the owner of a reference is what keeps it correct, and
    //! quietly rewriting a broken one would destroy the evidence of that owner's bug.
    template<typename E>
    bool EntityToJsonField(const E& handle, JsonValue& out)
    {
        return SerializeDetail::HandleToJson(
            static_cast<uint32_t>(handle), handle == E{entt::null}, out);
    }

    template<typename E>
    bool EntityFromJsonField(const JsonValue& in, E& target)
    {
        uint32_t raw    = 0;
        bool     isNull = false;
        if (!SerializeDetail::HandleFromJson(in, raw, isNull))
        {
            return false;
        }

        target = isNull ? E{entt::null} : static_cast<E>(raw);
        return true;
    }
}
