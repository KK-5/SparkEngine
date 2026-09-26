#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>

namespace Spark::PostProcess
{
    //! Makes its entity a post-process volume: the settings components beside it (Bloom, ...)
    //! apply to every view, each group on its own. Where several volumes set one group, the
    //! highest priority wins; a camera's own settings component overrides them all.
    //!
    //! Volumes are unbound for now: no shape, so they apply everywhere. Bounds, blend radius and
    //! blend weight join this component when a scene needs a local volume.
    struct PostProcessVolumeComponent
    {
        //! Volumes setting the same group must not share a priority: which one wins would
        //! depend on iteration order.
        int32_t m_priority = 0;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(PostProcess::PostProcessVolumeComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
