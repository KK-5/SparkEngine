#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>

namespace Spark::AntiAliasing
{
    enum class TemporalAAJitterSamples : uint32_t
    {
        Four    = 4,
        Eight   = 8,
        Sixteen = 16,
    };

    //! On a camera entity: its view runs temporal anti-aliasing. Presence enables it.
    struct TemporalAAComponent
    {
        //! Weight of the current frame when static. Lower is smoother, higher reacts faster.
        float m_currentFrameWeight = 1.0f / 16.0f;
        //! Weight of the current frame under fast motion; at least the static weight.
        float m_motionFrameWeight = 0.25f;
        //! Clip box half-width in standard deviations. Wider flickers less, ghosts more.
        float m_varianceClipGamma = 1.25f;
        //! Width of the current-frame reconstruction filter. Wider is softer and steadier.
        float m_filterSize = 1.0f;
        //! Length of the jitter sequence. More needs a lower frame weight to pay off.
        TemporalAAJitterSamples m_jitterSamples = TemporalAAJitterSamples::Eight;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(AntiAliasing::TemporalAAComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
