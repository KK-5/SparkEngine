#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>

namespace Spark::ScreenSpaceReflection
{
    //! Higher lets a ray go further before it gives up, for more time per pixel. What each
    //! level costs is the renderer's to choose.
    enum class ScreenSpaceReflectionQuality : uint32_t
    {
        Low,
        Medium,
        High,
    };

    //! On a post-process volume: the screen-space reflections it sets for every view. On a
    //! camera: overrides the volumes for that camera's view. They show what is on screen, and
    //! only in a view that runs temporal AA; everywhere else the environment's reflection
    //! stays.
    struct ScreenSpaceReflectionComponent
    {
        //! How much of the traced reflection replaces the environment's. Zero turns it off,
        //! which is how a volume overrides a lower one's reflections away.
        float m_intensity = 1.0f;

        //! Surfaces rougher than this keep the environment's reflection. The traced one is a
        //! mirror's, so it fades out toward this instead of blurring.
        float m_maxRoughness = 0.4f;

        ScreenSpaceReflectionQuality m_quality = ScreenSpaceReflectionQuality::Medium;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(ScreenSpaceReflection::ScreenSpaceReflectionComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
