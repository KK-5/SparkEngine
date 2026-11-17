#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>

namespace Spark::AntiAliasing
{
    enum class TemporalAAJitterSamples : uint32_t
    {
        Four,
        Eight,
        Sixteen,
    };

    //! On a camera entity: its view runs temporal anti-aliasing. Presence enables it.
    struct TemporalAAComponent
    {
        //! Variance box width under motion, in standard deviations (Intel quality path).
        float m_minVarianceGamma = 0.75f;
        //! Variance box width when stationary. Wider preserves detail but admits more history.
        float m_maxVarianceGamma = 2.0f;
        //! Motion at which history is discarded, in pixels/frame at 1080p; scales with view height.
        float m_motionRejectionPixels = 128.0f;
        //! Depth bias as a fraction of the far clip distance, matching MiniEngine's normalized Z.
        //! E.g. 0.001 at far=1000 allows 1 world unit. Smaller rejects closer depth separations.
        float m_depthRejectionBias = 0.001f;
        //! Length of the jitter sequence; accumulation weight is maintained in history alpha.
        TemporalAAJitterSamples m_jitterSamples = TemporalAAJitterSamples::Eight;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(AntiAliasing::TemporalAAComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
