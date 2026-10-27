#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>
#include <Tick/FrameTime.h>

#include <Staging/StagingBuffer.h>
#include <View/View.h>

#include "ViewBinding.h"

namespace Spark::Render
{
    //! Encodes every live view — the single encoding step, deliberately blind to who
    //! produced the view. A camera view, a shadow view and a sample's hand-built view all
    //! reach the GPU through here, so a producer only ever writes the View component.
    //!
    //! g_Views (space1) is small and changes every frame, so it is a StagingBuffer rewritten
    //! whole: the live views packed densely, each given its row as a ViewIndex. A pass
    //! reaches the array through .Binds<ViewBindingTag>(), and the row through the viewIndex
    //! the executer writes at each view handle.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced AFTER every view producer and before the graph runs,
    //! so a view created this frame is encoded in the same frame.
    //!
    //! Also the one place a ViewHistory rolls forward, so no producer tracks last frame.
    class ViewBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update(const FrameTime& time);
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Copies m_views into this frame's buffer in one go and binds g_Views to it. False
        //! when the buffer could not grow: then g_Views is bound to nothing.
        bool WriteAndBind(RHI::RHIContext& rhiCtx);

        StagingBuffer           m_viewBuffer;
        eastl::vector<ViewData> m_views;

        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Views @ space1
    };
}
