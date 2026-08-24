#pragma once

#include <Reflection/RTTI.h>

#include "Json.h"

namespace Spark
{
    //! Reflection-driven JSON serialization. Only fields carrying
    //! MetaFieldTraits::Serializable are visited; everything else is skipped in silence.
    //!
    //! Fields equal to the default-constructed instance's are omitted, at every object
    //! level, so a file only records what differs from the defaults.

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
