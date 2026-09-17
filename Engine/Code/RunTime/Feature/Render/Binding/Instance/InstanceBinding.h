#pragma once

#include <Binding/GlobalBuffer.h>

#include "InstanceData.h"

namespace Spark::Render
{
    //! Names the g_Instances array (space4) for GlobalBuffer and its slot refs.
    struct Instances {};

    //! A renderable's g_Instances slot, on the WORLD entity. Get() is the GPU index
    //! directly — there is no indirection table, and it does not move for the
    //! renderable's life, so a consumer bakes it once as StartInstanceLocation.
    using InstanceSlotRef = SlotRef<Instances>;

    //! What such a consumer holds instead of a copy. A copy would keep the slot
    //! allocated, and noticing that the renderable let it go is the whole point.
    using InstanceSlotWeakRef = SlotWeakRef<Instances>;

    //! The model matrix a renderable was encoded with last frame, on the WORLD entity.
    //! Absent means no history: the previous transform is taken as the current one.
    struct InstanceHistory
    {
        Math::Matrix4X4 m_model{Math::Matrix4X4Const::IDENTITY};
    };

    //! The single shared ShaderBindings entity carrying g_Instances (space4).
    struct InstanceBindingTag {};

    //! The single shared per-instance vertex stream ID buffer entity.
    struct InstanceIDBufferTag {};
}
