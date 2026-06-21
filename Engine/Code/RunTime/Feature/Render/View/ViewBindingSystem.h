#pragma once

#include <Base.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>

namespace Spark::Render
{
    //! Per-view frequency system. Builds ONE shared ViewBindings (space0) entity —
    //! its layout reflected straight from ViewBindings.hlsl — tags it MainViewTag,
    //! and refreshes g_ViewProjection from the camera each frame. Consumers fetch
    //! the single shared binding via GetView<MainViewTag, Components::ShaderBindings>,
    //! so every pass's draws can reference one binding instead of each building its own.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced BEFORE the Processors so the binding is
    //! materialized and up to date before any draw references it.
    class ViewBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        Ptr<RHI::PipelineLayoutDescriptor> m_layout;        //!< Keeps the ViewBindings layout alive.
        Ptr<RHI::ShaderBindings>           m_viewBindings;
        RHI::RHIHandle                     m_viewEntity = RHI::NullHandle;
    };
}
