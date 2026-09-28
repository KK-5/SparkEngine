#pragma once

#include <RHI/Context/RHIContext.h>
#include <Tick/FrameTime.h>

#include <View/View.h>

#include "ViewBinding.h"

namespace Spark::Render
{
    //! Encodes every live view — the single encoding step, deliberately blind to who
    //! produced the view. A camera view, a shadow view and a sample's hand-built view all
    //! reach the GPU through here, so a producer only ever writes the View component.
    //!
    //! Each view gets a stable g_Views slot (space1) and its row is rewritten every frame. A
    //! pass reaches the array through .Binds<ViewBindingTag>(), and the row through the
    //! viewIndex the executer writes at each view handle.
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
        //! frameIndex is the in-flight slot (swap-chain GetCurrentImageIndex), used to
        //! pick this frame's g_Views copy.
        void Update(uint32_t frameIndex, const FrameTime& time);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound on live views, shadow faces included. Overflow logs and drops
        //! the surplus. 256 * 592B = 148 KB per frame copy.
        static constexpr uint32_t Capacity = 256;

        GlobalBuffer<Views, ViewData, View> m_views;

        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Views @ space1
    };
}
