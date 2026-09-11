#include "EntityJson.h"

#include <nlohmann/json.hpp>

#include <Log/ILogSystem.h>

namespace Spark::SerializeDetail
{
    bool HandleToJson(uint32_t raw, bool isNull, JsonValue& out)
    {
        if (isNull)
        {
            out = nullptr;
        }
        else
        {
            out = raw;
        }
        return true;
    }

    bool HandleFromJson(const JsonValue& in, uint32_t& raw, bool& isNull)
    {
        if (in.is_null())
        {
            isNull = true;
            return true;
        }

        if (!in.is_number_unsigned())
        {
            LOG_WARN("[JsonSerializer] An entity identifier reads as a number or null, got {}.",
                in.type_name());
            return false;
        }

        isNull = false;
        raw    = in.get<uint32_t>();
        return true;
    }
}
