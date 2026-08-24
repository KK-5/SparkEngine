#include "JsonSerializer.h"

#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <EASTL/string.h>

#include <Log/ILogSystem.h>

#include "MetaFieldTraits.h"

namespace Spark
{
    namespace
    {
        bool IsSerializable(const MetaData& data)
        {
            return HasFieldTrait(data.traits<MetaFieldTraits>(), MetaFieldTraits::Serializable);
        }

        const char* TypeName(const MetaType& type)
        {
            const char* name = type ? type.name() : nullptr;
            return name ? name : "<unnamed>";
        }

        // ---- arithmetic leaves ----------------------------------------------------
        //
        // The table below names the fundamental types, not the fixed-width aliases:
        // int8_t is only another spelling of signed char, while `long` is a third 32-bit
        // type on MSVC that no fixed-width alias covers. try_cast matches exactly, so a
        // value is never widened, narrowed or sign-flipped on its way through.

        template<typename... Ts>
        struct ArithmeticList {};

        using Arithmetics = ArithmeticList<bool, char, signed char, unsigned char,
                                           short, unsigned short, int, unsigned int,
                                           long, unsigned long, long long, unsigned long long,
                                           float, double>;

        template<typename T>
        bool WriteArithmetic(const MetaAny& value, JsonValue& out)
        {
            const T* typed = value.try_cast<T>();
            if (typed == nullptr)
            {
                return false;
            }
            out = *typed;
            return true;
        }

        template<typename T>
        bool ReadArithmetic(const JsonValue& in, MetaAny& target, bool& ok)
        {
            T* typed = target.try_cast<T>();
            if (typed == nullptr)
            {
                return false;
            }

            const bool kindMatches = std::is_same<T, bool>::value ? in.is_boolean() : in.is_number();
            if (kindMatches)
            {
                *typed = in.get<T>();
            }
            else
            {
                LOG_ERROR("[JsonSerializer] Expected a number, got {}.", in.type_name());
                ok = false;
            }
            return true;
        }

        //! Both run the table until one entry claims the value by exact type. The return
        //! says whether it was claimed at all; whether the claim then succeeded is `ok`.
        template<typename... Ts>
        bool WriteAnyArithmetic(ArithmeticList<Ts...>, const MetaAny& value, JsonValue& out)
        {
            return (WriteArithmetic<Ts>(value, out) || ...);
        }

        template<typename... Ts>
        bool ReadAnyArithmetic(ArithmeticList<Ts...>, const JsonValue& in, MetaAny& target, bool& ok)
        {
            return (ReadArithmetic<Ts>(in, target, ok) || ...);
        }

        // ---- enums ----------------------------------------------------------------
        //
        // Enumerators are matched by value, never by their position in the reflected list:
        // an enum whose values are explicit or non-contiguous would otherwise map to the
        // wrong name. Taken by value so allow_cast has an any of its own to convert.

        bool EnumToInt(MetaAny value, int64_t& out)
        {
            if (!value.allow_cast<int64_t>())
            {
                return false;
            }
            out = value.cast<int64_t>();
            return true;
        }

        bool WriteEnum(const MetaType& type, const MetaAny& value, JsonValue& out)
        {
            int64_t current = 0;
            if (!EnumToInt(value, current))
            {
                return false;
            }

            for (auto&& [id, data] : type.data())
            {
                int64_t enumerator = 0;
                if (data.name() != nullptr && EnumToInt(data.get({}), enumerator)
                    && enumerator == current)
                {
                    out = data.name();
                    return true;
                }
            }

            LOG_WARN("[JsonSerializer] {} holds a value no reflected enumerator names.",
                TypeName(type));
            return false;
        }

        bool ReadEnum(const MetaType& type, const JsonValue& in, MetaAny& target)
        {
            if (!in.is_string())
            {
                LOG_ERROR("[JsonSerializer] {} expects an enumerator name, got {}.",
                    TypeName(type), in.type_name());
                return false;
            }

            const std::string name = in.get<std::string>();
            for (auto&& [id, data] : type.data())
            {
                if (data.name() != nullptr && name == data.name())
                {
                    return target.assign(data.get({}));
                }
            }

            // Keeps the target's current value: an enumerator this build does not know is a
            // data error, not a reason to leave the object half-written.
            LOG_ERROR("[JsonSerializer] {} has no enumerator named '{}'.",
                TypeName(type), name.c_str());
            return false;
        }

        // ---- sequences ------------------------------------------------------------

        bool WriteSequence(const MetaAny& value, JsonValue& out)
        {
            auto view = value.as_sequence_container();
            if (!view)
            {
                return false;
            }

            out = JsonValue::array();
            bool ok = true;
            for (auto&& element : view)
            {
                JsonValue encoded;
                if (SerializeToJson(element, encoded))
                {
                    out.push_back(std::move(encoded));
                }
                else
                {
                    ok = false;
                }
            }
            return ok;
        }

        bool ReadSequence(const JsonValue& in, MetaAny& target)
        {
            if (!in.is_array())
            {
                LOG_ERROR("[JsonSerializer] Expected an array, got {}.", in.type_name());
                return false;
            }

            auto view = target.as_sequence_container();
            if (!view || !view.resize(in.size()))
            {
                return false;
            }

            bool ok = true;
            size_t index = 0;
            for (auto element : view)
            {
                if (!DeserializeFromJson(in[index], element))
                {
                    ok = false;
                }
                ++index;
            }
            return ok;
        }

        // ---- objects --------------------------------------------------------------

        bool WriteObject(const MetaType& type, const MetaAny& value, JsonValue& out)
        {
            out = JsonValue::object();

            // Defaults are encoded and compared as JSON subtrees rather than as values:
            // entt only generates a comparison for equality-comparable types, so a nested
            // struct or a vector field would always claim to differ and never be omitted.
            MetaAny defaults = type.construct();

            bool ok = true;
            for (auto&& [id, data] : type.data())
            {
                if (!IsSerializable(data) || data.name() == nullptr)
                {
                    continue;
                }

                MetaAny field = data.get(value);
                JsonValue encoded;
                if (!field || !SerializeToJson(field, encoded))
                {
                    LOG_WARN("[JsonSerializer] {}::{} is marked Serializable but cannot be encoded.",
                        TypeName(type), data.name());
                    ok = false;
                    continue;
                }

                if (defaults)
                {
                    MetaAny defaultField = data.get(defaults);
                    JsonValue defaultEncoded;
                    if (defaultField && SerializeToJson(defaultField, defaultEncoded)
                        && defaultEncoded == encoded)
                    {
                        continue;
                    }
                }

                out[data.name()] = std::move(encoded);
            }
            return ok;
        }

        bool ReadObject(const MetaType& type, const JsonValue& in, MetaAny& target)
        {
            if (!in.is_object())
            {
                LOG_ERROR("[JsonSerializer] {} expects an object, got {}.",
                    TypeName(type), in.type_name());
                return false;
            }

            bool ok = true;
            for (auto&& [id, data] : type.data())
            {
                if (!IsSerializable(data) || data.name() == nullptr)
                {
                    continue;
                }

                const auto entry = in.find(data.name());
                if (entry == in.end())
                {
                    continue;
                }

                // get() hands back a copy, not a reference: entt's default data policy is
                // as_is. The copy is filled and then written back through set().
                MetaAny field = data.get(target);
                if (!field || !DeserializeFromJson(*entry, field) || !data.set(target, field))
                {
                    ok = false;
                }
            }
            return ok;
        }
    }

    bool SerializeToJson(const MetaAny& value, JsonValue& out)
    {
        // Captured before anything casts the value: entt stops reporting an enum as one
        // once it has been through a cast.
        const MetaType type = value.type();
        if (!type)
        {
            return false;
        }

        if (type.is_enum())
        {
            return WriteEnum(type, value, out);
        }

        if (WriteAnyArithmetic(Arithmetics{}, value, out))
        {
            return true;
        }

        if (const eastl::string* text = value.try_cast<eastl::string>())
        {
            out = std::string(text->c_str(), text->size());
            return true;
        }

        if (type.is_sequence_container())
        {
            return WriteSequence(value, out);
        }

        // Last, because eastl::string is a class too and would otherwise land here and
        // quietly encode as an empty object.
        if (type.is_class())
        {
            return WriteObject(type, value, out);
        }

        LOG_WARN("[JsonSerializer] No encoding for type {}.", TypeName(type));
        return false;
    }

    bool DeserializeFromJson(const JsonValue& in, MetaAny& target)
    {
        const MetaType type = target.type();
        if (!type)
        {
            return false;
        }

        if (type.is_enum())
        {
            return ReadEnum(type, in, target);
        }

        bool ok = true;
        if (ReadAnyArithmetic(Arithmetics{}, in, target, ok))
        {
            return ok;
        }

        if (eastl::string* text = target.try_cast<eastl::string>())
        {
            if (!in.is_string())
            {
                LOG_ERROR("[JsonSerializer] Expected a string, got {}.", in.type_name());
                return false;
            }
            const std::string decoded = in.get<std::string>();
            text->assign(decoded.c_str(), decoded.size());
            return true;
        }

        if (type.is_sequence_container())
        {
            return ReadSequence(in, target);
        }

        if (type.is_class())
        {
            return ReadObject(type, in, target);
        }

        LOG_WARN("[JsonSerializer] No decoding for type {}.", TypeName(type));
        return false;
    }
}
