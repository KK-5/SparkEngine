#pragma once

#include <EASTL/vector.h>

#include <Base.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

namespace Spark::Render
{
    //! The shared groups the graph owns: one per shared space, created the first time a frame
    //! needs one and reused after that. Only the shared spaces are here — the per-pass space and
    //! the root-constant space have no group of their own, and asking for one asserts.
    //!
    //! A group is built from the graph's declaration for its space (SharedGroupLayouts.h), so a
    //! system neither creates one nor keeps a handle to it.
    class SharedBindingPool
    {
    public:
        //! The group for `spaceId`. Built on first use from the space's declaration; null when
        //! nothing declares that space, which the caller reports.
        RHI::ShaderBindings* Get(uint32_t spaceId);

        //! Destroy every group, and with it the views they hold. Must run before the pools those
        //! views came from.
        void Shutdown();

    private:
        eastl::vector<Ptr<RHI::ShaderBindings>> m_groups;
    };
}
