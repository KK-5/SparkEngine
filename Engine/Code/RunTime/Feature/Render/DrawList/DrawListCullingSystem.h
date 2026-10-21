#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <RHI/Command/IndirectCommands.h>
#include <RHI/Context/RHIContext.h>

#include <Pass/PassCapabilities.h>
#include <Pass/PassContext.h>

#include "DrawContext.h"

namespace Spark::Render
{
    //! Culls the lists the CPU culls: tests each DrawListInstance against the frustum of each
    //! view a pass draws its list under, writes those it sees as VisibleInstances, sorted, and
    //! their draws into the CpuCulledDrawArgumentsTag buffer in that order. What
    //! InstanceCullingPass and the InstanceBinning passes are to the lists the GPU culls.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, updated after InstanceBindingSystem
    //! (whose DrawListInstances it reads) and before DrawGroupSystem.
    class DrawListCullingSystem
    {
    public:
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        void WriteDrawArguments(RHI::RHIContext& rhiCtx, const DrawContext& drawCtx);

        //! Each view and list bit once, however many passes draw that list under that view.
        eastl::fixed_vector<eastl::pair<RHI::RHIHandle, uint32_t>, 8> m_lists;
        ViewHandleList m_views;

        RHI::RHIHandle m_drawArguments = RHI::NullHandle;
        //! Draws it has room for, rounded up a step at a time.
        uint32_t       m_drawArgumentsCapacity = 0;
        //! What it is copied from at the frame's start, so it stays put until then.
        eastl::vector<RHI::DrawIndexedIndirectCommand> m_drawArgumentsData;
    };
}
