#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>
#include <Reflection/ReflectContext.h>
#include <Serialization/MetaFieldTraits.h>

namespace SceneTest
{
    //! Reflected, with a Serializable field, but not Persistent: it must not reach the file.
    struct Scratch
    {
        int32_t value = 0;
    };

    //! Persistent with no fields -- the type itself is the data, written as {}.
    struct Marker {};
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(SceneTest::Marker,
        static constexpr ComponentFlags flags = ComponentFlags::Persistent;
    )
}

namespace SceneTest
{
    inline void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<Scratch>().Type("Scratch")
            .Data<&Scratch::value>("Value").Traits(Spark::MetaFieldTraits::Serializable);

        context.Reflect<Marker>().Type("Marker").Traits(Spark::ComponentTraits<Marker>::flags);
    }
}
