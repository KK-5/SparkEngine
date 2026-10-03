#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>

namespace Spark::AmbientOcclusion
{
    //! Higher leaves less noise, for more time in the main pass; the occlusion itself stays the
    //! same. What each level costs is the renderer's to choose.
    enum class AmbientOcclusionQuality : uint32_t
    {
        Low,
        Medium,
        High,
    };

    //! On a post-process volume: the screen-space ambient occlusion it sets for every view. On
    //! a camera: overrides the volumes for that camera's view. Where nothing sets it, indirect
    //! light is occluded by the materials' own occlusion maps only.
    struct AmbientOcclusionComponent
    {
        //! How much of the occlusion is applied. Zero turns it off, which is how a volume
        //! overrides a lower one's ambient occlusion away.
        float m_intensity = 1.0f;

        //! World-space reach of the occlusion: geometry further than this from a point does
        //! not darken it.
        float m_radius = 0.5f;

        AmbientOcclusionQuality m_quality = AmbientOcclusionQuality::Low;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(AmbientOcclusion::AmbientOcclusionComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
