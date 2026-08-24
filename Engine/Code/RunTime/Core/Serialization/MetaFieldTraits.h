#pragma once

#include <cstdint>

#include <Math/Bit.h>

namespace Spark
{
    //! Per-field reflection traits, attached with .Data<&T::field>("Name").Traits(...).
    //!
    //! Distinct from MetaTypeTraits, which is type-level and on its way out in favour of
    //! ComponentTraits. Field-level metadata cannot ride on Custom either: entt keeps a
    //! single custom slot per element, and a field's is already taken by its UIElement.
    enum class MetaFieldTraits : uint8_t
    {
        None         = 0,

        //! Persisted. Serialization is opt-in because reflection here exists for the
        //! inspector first: without the opt-in, every read-only display field a panel
        //! needs would silently become part of the on-disk format.
        Serializable = 1 << 0,
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::MetaFieldTraits, uint8_t);

    constexpr bool HasFieldTrait(MetaFieldTraits traits, MetaFieldTraits query)
    {
        return (traits & query) == query;
    }
}
