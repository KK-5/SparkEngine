#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>

#include <RHI/Context/RHIHandle.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/RHILimits.h>
#include <RHI/Scissor/Scissor.h>
#include <RHI/Viewport/Viewport.h>

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::Render
{
    //! A per-pass-per-Drawable draw recipe. Stitches a persistent Drawable
    //! (which carries the per-object data: geometry, instance binding,
    //! geometry/instancing args, slot ref) together with the Pass-side
    //! injection (view binding, viewport/scissor, optional PSO override).
    //! CompileDrawRequests resolves it into a DrawItem each frame.
    struct DrawRequest
    {
        //! The Drawable this request draws. Required.
        RHI::RHIHandle m_drawable = RHI::NullHandle;

        //! Per-pass space0 ShaderBindings entity. The Pass injects it each
        //! frame; consumers never long-term cache this handle elsewhere.
        RHI::RHIHandle m_viewBinding = RHI::NullHandle;

        //! Per-pass viewport / scissor; the Processor fills them per Pass.
        uint8_t m_viewportsCount = 0;
        uint8_t m_scissorsCount  = 0;
        eastl::fixed_vector<RHI::Viewport, RHI::Limits::Pipeline::AttachmentColorCountMax> m_viewports;
        eastl::fixed_vector<RHI::Scissor,  RHI::Limits::Pipeline::AttachmentColorCountMax> m_scissors;

        //! Per-draw stencil reference.
        uint8_t m_stencilRef = 0;

        //! Optional per-draw PSO variant: override the pass-level vertex /
        //! fragment shader and/or fixed-function render states. When all
        //! three are default the draw inherits the pass PSO. (Lifecycle of
        //! these is TBD with the Material system.)
        Ptr<Resource::ShaderAsset>         m_vertexShaderOverride;
        Ptr<Resource::ShaderAsset>         m_fragmentShaderOverride;
        eastl::optional<RHI::RenderStates> m_renderStatesOverride;
    };
}
