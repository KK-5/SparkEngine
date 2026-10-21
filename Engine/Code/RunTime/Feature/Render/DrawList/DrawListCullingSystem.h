#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/utility.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassCapabilities.h>
#include <Pass/PassContext.h>

#include "DrawContext.h"

namespace Spark::Render
{
    //! Culls the lists the CPU culls: tests each DrawListInstance against the frustum of each
    //! view a pass draws its list under, and writes those it sees as VisibleInstances, sorted.
    //! What InstanceCullingPass is to the lists the GPU culls.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, updated after InstanceBindingSystem
    //! (whose DrawListInstances it reads) and before DrawGroupSystem.
    class DrawListCullingSystem
    {
    public:
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx);

    private:
        //! Each view and list bit once, however many passes draw that list under that view.
        eastl::fixed_vector<eastl::pair<RHI::RHIHandle, uint32_t>, 8> m_lists;
        ViewHandleList m_views;
    };
}
