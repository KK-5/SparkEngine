#pragma once

#include <Binding/GlobalBuffer.h>

#include "InstanceData.h"

namespace Spark::Render
{
    //! Names the g_Instances array (space4) for GlobalBuffer and its slot refs.
    struct Instances {};

    //! A renderable's reference to its g_Instances slot, on the WORLD entity. m_id is the
    //! GPU index directly — there is no indirection table, and it does not move for the
    //! renderable's life, so a consumer may copy the ref and bake m_id as
    //! StartInstanceLocation. IsValid() is what tells a stale copy apart from a live one.
    using InstanceSlotRef = GlobalBufferSlotRef<Instances>;

    //! The single shared ShaderBindings entity carrying g_Instances (space4).
    struct InstanceBindingTag {};

    //! The single shared per-instance vertex stream ID buffer entity.
    struct InstanceIDBufferTag {};
}
