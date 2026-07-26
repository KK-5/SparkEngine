#pragma once

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <EASTL/functional.h>

#include <RHI/Factory.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Pass/PassBuilder.h>   // RenderPassConfig, BuildPipelineLayoutFromShaders, shared components
#include <Drawable/DrawItemRoute.h>
#include <Drawable/DrawItemBind.h>
#include <RenderGraph/RenderGraphCompiler.h>
#include <RenderGraph/RenderGraphExecuter.h>

namespace Spark::Render
{
    //! Default Execute body for a render pass. Submits every compiled DrawItem
    //! tagged with PassTag to the pass's command list, in pool order.
    template<typename PassTag>
    void SubmitPassDrawItems(ExecuteWork& work, RenderGraphExecuter&)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        ctx.GetView<PassTag, RHI::DrawItem>().each([&](RHIHandle, const RHI::DrawItem& item)
        {
            work.m_commandList->Submit(item);
        });
    }

    // ================================================================
    // RenderPassBuilder<PassTag> — chainable builder for graphics passes
    //
    // Lives here (not in the common PassBuilder.h) because Finalize installs the
    // slot-resolving Compile/Execute defaults, which pull the heavy PassAccess.h
    // chain — kept out of the universally-included header so only files that write
    // render passes pay for it.
    // ================================================================
    template<typename PassTag>
    class RenderPassBuilder
    {
    public:
        using BuildFunction   = eastl::function<void(RenderGraphBuilder&)>;
        using CompileFunction = eastl::function<void(RenderGraphCompiler&)>;
        using ExecuteFunction = eastl::function<void(ExecuteWork&, RenderGraphExecuter&)>;

        RenderPassBuilder& Queue(RHI::HardwareQueueClass q)
        {
            m_queue = q;
            m_queueSet = true;
            return *this;
        }

        RenderPassBuilder& Inactive()
        {
            m_active = false;
            return *this;
        }

        // ---- Shaders ----
        RenderPassBuilder& VertexShader(Ptr<Resource::ShaderAsset> asset)
        {
            m_shaders.m_vertexShader = eastl::move(asset);
            return *this;
        }

        RenderPassBuilder& FragmentShader(Ptr<Resource::ShaderAsset> asset)
        {
            m_shaders.m_fragmentShader = eastl::move(asset);
            return *this;
        }

        RenderPassBuilder& GeometryShader(Ptr<Resource::ShaderAsset> asset)
        {
            m_shaders.m_geometryShader = eastl::move(asset);
            return *this;
        }

        // ---- Fixed-function pipeline state ----
        RenderPassBuilder& InputLayout(const RHI::InputStreamLayout& layout)
        {
            m_pipelineState.m_inputStreamLayout = layout;
            return *this;
        }

        RenderPassBuilder& RenderStates(const RHI::RenderStates& states)
        {
            m_pipelineState.m_renderStates = states;
            return *this;
        }

        RenderPassBuilder& RenderTargetLayout(const RHI::RenderTargetLayout& layout)
        {
            m_pipelineState.m_renderTargetLayout = layout;
            return *this;
        }

        // ---- Viewport / Scissor ----
        RenderPassBuilder& ViewportScissor(const RHI::Viewport& vp, const RHI::Scissor& scissor)
        {
            m_viewport = vp;
            m_scissor  = scissor;
            m_hasViewportScissor = true;
            return *this;
        }

        // ---- Custom pipeline (skip engine PSO) ----
        RenderPassBuilder& CustomPipeline()
        {
            m_customPipeline = true;
            return *this;
        }

        // ---- Draw-item routing ----
        // Declare the Drawable classifications this pass consumes; DrawableComposer
        // derives one DrawItem per accepted Drawable. Full-screen / procedural passes
        // omit this.
        template<typename... DrawTags>
        RenderPassBuilder& Accepts()
        {
            m_route.m_accepts = &AcceptDrawTags<DrawTags...>;
            m_route.m_marks   = &MarkPassTag<PassTag>;
            m_hasRoute        = true;
            return *this;
        }

        // Declare the shared SRGs (view / material / …, each a global singleton) this
        // pass injects into its DrawItems every frame. Order-free — each SRG self-
        // describes its HLSL space. The per-object SRG (space4) is baked at compose, and
        // the pass's own per-pass SRG (space2) is auto-injected via PassTag — neither is
        // listed here.
        template<typename... BindingTags>
        RenderPassBuilder& Binds()
        {
            m_route.m_bindPass = &BindPassDrawItems<PassTag, BindingTags...>;
            return *this;
        }

        // ---- Functions ----
        RenderPassBuilder& Build(BuildFunction fn)
        {
            m_buildFunction = eastl::move(fn);
            return *this;
        }

        RenderPassBuilder& Compile(CompileFunction fn)
        {
            m_compileFunction = eastl::move(fn);
            return *this;
        }

        RenderPassBuilder& Execute(ExecuteFunction fn)
        {
            m_executeFunction = eastl::move(fn);
            return *this;
        }

        // ---- Finalize ----
        Pass Finalize()
        {
            ASSERT(!m_finalized, "Pass '{}' is already finalized.", m_name.GetCStr());
            ASSERT(m_queueSet, "Pass '{}': Queue must be set.", m_name.GetCStr());
            ASSERT(m_buildFunction, "Pass '{}': Build function is required.", m_name.GetCStr());

            if (m_customPipeline)
            {
                ASSERT(!m_shaders.m_vertexShader && !m_shaders.m_fragmentShader &&
                       !m_shaders.m_geometryShader && !m_shaders.m_computeShader,
                    "Pass '{}': CustomPipeline pass must not set shaders.", m_name.GetCStr());
            }
            else
            {
                ASSERT(m_shaders.m_vertexShader || m_shaders.m_fragmentShader,
                    "Pass '{}': RenderPass needs at least VertexShader or FragmentShader.",
                    m_name.GetCStr());
                // A render pass must write at least one attachment — a color
                // target OR a depth-stencil target. Depth-only passes (depth
                // prepass, shadow) have zero color attachments but a valid
                // depth-stencil format, which IsEmpty() correctly accepts.
                ASSERT(!m_pipelineState.m_renderTargetLayout.IsEmpty(),
                    "Pass '{}': RenderPass needs at least one color target or a "
                    "depth-stencil format.",
                    m_name.GetCStr());
            }

            Pass pass = m_context->CreatePass();

            // Tag the entity with its compile-time PassTag so external code can
            // do O(1) lookup via GetView<SPARK_PASS_TAG("Name")>(). Asserting
            // view emptiness here catches both duplicate registration and the
            // (vanishingly rare) 32-bit FNV-1a hash collision at fail-fast time.
            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<RenderPassTag>(pass);
            m_context->Add<PassExecuteQueue>(pass, PassExecuteQueue{m_queue});
            m_context->Add<PassAttachmentMarker>(pass, MarkPassAttachmentCompiling<PassTag>());

            if (m_active)
                m_context->Add<ActivePassTag>(pass);

            if (m_hasViewportScissor)
                m_context->Add<PassViewportState>(pass, PassViewportState{m_viewport, m_scissor});

            m_context->Add<PassShaders>(pass, m_shaders);

            if (m_customPipeline)
            {
                m_context->Add<CustomPipelinePassTag>(pass);
            }
            else
            {
                m_context->Add<PassPipelineState>(pass, m_pipelineState);

                // Eager build PipelineLayoutDescriptor from shader reflection so
                // user code (e.g. ShaderBindings::Init) can grab it before Compile.
                auto* factory = Service<RHI::Factory>::Get();
                ASSERT(factory, "Pass '{}': RHI::Factory service is not registered.",
                    m_name.GetCStr());
                if (auto layout = BuildPipelineLayoutFromShaders(*factory, m_shaders))
                {
                    m_context->Add<PassPipelineLayout>(pass, PassPipelineLayout{ eastl::move(layout) });
                }
            }

            // Install the slot-resolving defaults unless the caller overrode them.
            // m_compileFunction has no engine-provided default: the only consumer
            // was the now-removed AttachmentBinding flow, and per-pass binding
            // population is a separate Binding system's responsibility. Passes
            // that need a compile step opt in via .Compile(...). RenderGraph
            // already null-checks before invoking, so an empty slot is fine.
            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
            funcs.m_compileFunction = eastl::move(m_compileFunction);
            funcs.m_executeFunction = m_executeFunction
                ? eastl::move(m_executeFunction)
                : ExecuteFunction(SubmitPassDrawItems<PassTag>);
            m_context->Add<PassFunctions>(pass, eastl::move(funcs));

            if (m_hasRoute)
            {
                m_context->Add<DrawItemRoute>(pass, m_route);
            }

            m_finalized = true;
            return pass;
        }

    private:
        template<typename T>
        friend RenderPassBuilder<T> RegisterRenderPass(PassContext&, ObjectName);

        RenderPassBuilder(PassContext& ctx, ObjectName name)
            : m_context(&ctx)
            , m_name(name)
        {
        }

        PassContext*            m_context;
        ObjectName              m_name;
        RHI::HardwareQueueClass m_queue {};
        bool                    m_active            {true};
        bool                    m_customPipeline    {false};

        DrawItemRoute           m_route {};
        bool                    m_hasRoute          {false};

        PassShaders             m_shaders;
        PassPipelineState       m_pipelineState;

        RHI::Viewport           m_viewport {};
        RHI::Scissor            m_scissor  {};
        bool                    m_hasViewportScissor{false};

        BuildFunction           m_buildFunction;
        CompileFunction         m_compileFunction;
        ExecuteFunction         m_executeFunction;

        bool                    m_queueSet  {false};
        bool                    m_finalized {false};
    };

    template<typename PassTag>
    RenderPassBuilder<PassTag> RegisterRenderPass(PassContext& ctx, ObjectName name)
    {
        return RenderPassBuilder<PassTag>(ctx, name);
    }
}

#define SPARK_RENDER_PASS(ctx, NAME) \
    ::Spark::Render::RegisterRenderPass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))
