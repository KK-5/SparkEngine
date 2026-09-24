#pragma once

#include <Base.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/variant.h>
#include <Object/ObjectName.h>

#include <RHI/Pipeline/InputStreamLayout.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Pipeline/RenderTargetLayout.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <Resource/Shader/ShaderAsset.h>

#include <Pass/Pass.h>
#include <RHI/Context/RHIContext.h>
#include <Pass/Component/RHIComponents.h>

namespace Spark::RHI
{
    class CommandList;
    class PipelineLayoutDescriptor;
    class ShaderBindings;
}

namespace Spark::Render
{
    class RenderGraphBuilder;
    class RenderGraphCompiler;
    class RenderGraphExecuter;
    struct ExecuteWork;


    struct RenderPassTag
    {
    };

    struct ComputePassTag
    {
    };

    //! Marks a pass that does raw command-list copy/blit/resolve work (no shaders,
    //! no PSO, no render targets). Discriminator alongside RenderPassTag /
    //! ComputePassTag; also the natural hook for future automated copy-command
    //! synthesis from declared attachments.
    struct CopyPassTag
    {
    };

    struct RayTracingPassTag
    {
    };

    struct ActivePassTag
    {
    };

    //! Marks a pass that manages its own pipeline state (e.g. ImGui), skipping
    //! engine-side PSO compilation. PSO compiler skips passes with this tag.
    struct CustomPipelinePassTag
    {
    };

    ////////////////////////////////////////////////////
    struct PassName
    {
        ObjectName m_name {};
    };

    struct PassExecuteQueue
    {
        RHI::HardwareQueueClass m_queue;
    };

    //! Index of the pass in RenderGraphBuilder::TopoSort()'s returned linear order.
    //! Assigned in TopoSort itself, so any code consuming the topo-sorted span can
    //! rely on `m_position == span index`. Used as the opaque ordering key for
    //! cross-queue sync (timeline semaphore values) and for transient resource
    //! lifetime overlap analysis.
    struct PassGlobalTimeline
    {
        uint32_t m_position {0};
    };

    //////////////////////////////
    // PSO components

    //! Fixed-function pipeline state: input layout, render-target formats,
    //! and raster/blend/depth states. Bundled into one component so the
    //! builder and PSO compiler don't need to track three separate pieces.
    struct PassPipelineState
    {
        RHI::InputStreamLayout  m_inputStreamLayout  {};
        RHI::RenderTargetLayout m_renderTargetLayout {};
        RHI::RenderStates       m_renderStates       {};
    };

    struct PassShaders
    {
        Ptr<Resource::ShaderAsset> m_vertexShader   = nullptr;
        Ptr<Resource::ShaderAsset> m_fragmentShader = nullptr;
        Ptr<Resource::ShaderAsset> m_geometryShader = nullptr;
        Ptr<Resource::ShaderAsset> m_computeShader  = nullptr;
    };

    //! PipelineLayoutDescriptor derived from PassShaders reflection at
    //! PassBuilder::Finalize. Lives on the pass so user-side Build callbacks
    //! can construct ShaderBindings (which require the layout at Init) without
    //! waiting for the render-graph Compile phase. PSO compiler reads this
    //! to assemble the root signature / descriptor set layouts.
    struct PassPipelineLayout
    {
        Ptr<RHI::PipelineLayoutDescriptor> m_layout;
    };

    //! Compiled PSO cache. Written by PSO compiler, read by executer.
    struct PassCompiledPSO
    {
        Ptr<RHI::PipelineState> m_pso;
    };

    //! The pass's own per-pass bindings plus the shared ones it declares via .Binds<>(),
    //! resolved once per frame and bound once before the pass's draws. They are identical
    //! for every draw in the pass, so they live here and not on the DrawItem.
    struct PassSharedBindings
    {
        eastl::fixed_vector<const RHI::ShaderBindings*, RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_bindings;
    };

    //! Forces PSO recompilation on next frame (set on shader hot-reload).
    struct PassPSODirtyTag
    {
    };
    //////////////////////////////

    struct PassFunctions
    {
        eastl::function<void(RenderGraphBuilder&)> m_buildFunction;
        eastl::function<void(RenderGraphCompiler&)> m_compileFunction;
        eastl::function<void(ExecuteWork&, RenderGraphExecuter&)> m_executeFunction;
    };
}