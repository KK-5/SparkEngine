#pragma once

#include <cstdint>

#include <ECS/ComponentTraits.h>

namespace Spark::Tonemap
{
    //! Blender's AgX looks. The contrast ones scale the log2 exposure around middle grey before
    //! AgX's curve, so the curve's toe and shoulder still hold black and white; the lower ones
    //! add a little saturation back and the higher ones take a little away, as Blender's do.
    enum class AgXLook : uint32_t
    {
        BaseContrast,
        VeryHighContrast,
        HighContrast,
        MediumHighContrast,
        MediumLowContrast,
        LowContrast,
        VeryLowContrast,
        Greyscale,
    };

    //! Turns on the AgX tone curve, graded by one of Blender's looks. Where nothing sets it the
    //! image gets a plain Reinhard curve instead.
    //!
    //! On a post-process volume: the tone mapping it sets for every view. On a camera: overrides
    //! the volumes for that camera's view.
    struct TonemapComponent
    {
        AgXLook m_look = AgXLook::BaseContrast;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Tonemap::TonemapComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
