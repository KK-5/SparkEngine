#pragma once

#include <ECS/ComponentTraits.h>

namespace Spark::Bloom
{
    //! On a post-process volume: the bloom it sets for every view. On a camera: overrides the
    //! volumes for that camera's view. Where nothing sets it, there is no bloom.
    struct BloomComponent
    {
        //! Fraction of the light scattered into the glow. It moves light rather than adds it:
        //! the image keeps its energy, so raising this never brightens the frame as a whole.
        //! Zero turns bloom off, which is how a volume overrides a lower one's bloom away.
        float m_intensity = 0.04f;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Bloom::BloomComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
