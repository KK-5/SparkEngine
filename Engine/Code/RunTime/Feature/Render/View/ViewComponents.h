#pragma once

#include <RHI/Context/RHIHandle.h>

namespace Spark::Render
{
    //! View entity -> its space1 ShaderBindings entity.
    struct ViewShaderBindings
    {
        RHI::RHIHandle m_bindings = RHI::NullHandle;
    };

    //! Source -> the MainViewTag view entity it produced. Lives on the WORLD entity (a
    //! camera today), like InstanceSlotRef. Named after the view TYPE, not the source:
    //! one camera can later source several types (a planar reflection view is derived
    //! from it too). Shadows get their own ShadowViewRefs on the light — N per source.
    struct MainViewRef
    {
        RHI::RHIHandle m_view = RHI::NullHandle;
    };
}
