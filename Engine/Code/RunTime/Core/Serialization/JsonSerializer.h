#pragma once

#include <entt/core/hashed_string.hpp>

#include <Reflection/ReflectContext.h>
#include <Reflection/RTTI.h>

#include "Json.h"

namespace Spark
{
    //! A type's own JSON encoding, looked up ahead of every built-in branch and taking over
    //! completely once found. For types a field walk cannot rebuild, like AssetId.
    //!
    //! Same rule as operator<<: an operation may hand SUB-PARTS back to SerializeToJson,
    //! never its own value.
    struct JsonOperation
    {
        bool (*toJson)(const MetaAny& value, JsonValue& out);
        bool (*fromJson)(const JsonValue& in, MetaAny& target);
    };

    inline constexpr const char* kJsonOperationName = "JsonOperation";
    inline constexpr TypeId      kJsonOperationId   = entt::hashed_string::value(kJsonOperationName);

    namespace SerializeDetail
    {
        //! T is known here, so To's and From's signatures are checked at instantiation
        //! rather than failing as an empty invoke at runtime.
        template<typename T, auto To, auto From>
        struct JsonOperationFor
        {
            static bool ToJson(const MetaAny& value, JsonValue& out)
            {
                const T* typed = value.try_cast<T>();
                return typed != nullptr && To(*typed, out);
            }

            static bool FromJson(const JsonValue& in, MetaAny& target)
            {
                T* typed = target.try_cast<T>();
                return typed != nullptr && From(in, *typed);
            }

            static JsonOperation Get() { return {&ToJson, &FromJson}; }
        };
    }

    //! Registers T's pair. To must be callable as `bool(const T&, JsonValue&)`, From as
    //! `bool(const JsonValue&, T&)`.
    template<typename T, auto To, auto From>
    void ReflectJsonOperation(ReflectContext& context)
    {
        context.Reflect<T>()
            .Func<&SerializeDetail::JsonOperationFor<T, To, From>::Get>(kJsonOperationName);
    }

    //! Reflection-driven JSON serialization. Only fields carrying
    //! MetaFieldTraits::Serializable are visited; everything else is skipped in silence.
    //!
    //! Every visited field is written, including ones that happen to equal a default. The
    //! file is self-describing, and changing a default in code cannot silently restate what
    //! files already on disk mean. Versioning does not depend on this -- it rests on the
    //! READ side's "missing key keeps the current value".

    //! Returns false when a field marked Serializable could not be encoded. `out` keeps
    //! whatever was written up to that point -- there is no rollback.
    bool SerializeToJson(const MetaAny& value, JsonValue& out);

    //! Writes into `target`, which must name an existing object: either a reference any
    //! (MetaType::from_void(&obj)) or an owning any the caller then reads back.
    //!
    //! A key missing from `in` leaves the target's current value alone, which is what makes
    //! "field added since the file was written" a non-event. Best effort: one bad field does
    //! not stop the others, it only shows up in the return value.
    bool DeserializeFromJson(const JsonValue& in, MetaAny& target);
}
