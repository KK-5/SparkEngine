#pragma once

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <EASTL/functional.h>

#include <RHI/Factory.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Pass/PassBuilder.h>
#include <Pass/PassCapabilities.h>
#include <RenderGraph/RenderGraphExecuter.h>

namespace Spark::Render
{
    // ================================================================
    // RenderPassBuilder<PassTag> — chainable builder for graphics passes
    //
    // Lives here (not in the common PassBuilder.h) because it pulls PassCapabilities.h,
    // kept out of the universally-included header so only files that write render
    // passes pay for it.
    // ================================================================
    template<typename PassTag>
    class RenderPassBuilder
    {
    public:
        using BuildFunction   = eastl::function<void(RenderGraphBuilder&)>;
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

        // Declare the shared bindings (view / material / instance / …, each a global
        // singleton) the executer binds once before this pass's draws. Order-free — each
        // self-describes its HLSL space. The pass's own group (space2) is resolved via
        // PassBindings and is not listed here.
        template<typename... BindingTags>
        RenderPassBuilder& Binds()
        {
            m_capabilities.m_resolveSharedBindings = &ResolveSharedBindings<BindingTags...>;
            m_hasCapabilities                      = true;
            return *this;
        }

        // ---- Views ----
        // Declare which TYPE of view this pass renders: one DrawList per live view
        // instance of that type. A single tag, not a pack — one pass rendering both the
        // main view and shadow views makes no sense (attachments, PSO and RT layout all
        // differ), so the signature makes it impossible.
        //
        // Required of every pass that draws: viewport / scissor and the space1 bindings
        // both come from the view, so a pass with draws and no view has neither.
        template<typename ViewTag>
        RenderPassBuilder& RendersView()
        {
            m_capabilities.m_collectViews = &CollectViews<ViewTag>;
            m_hasCapabilities             = true;
            return *this;
        }

        // ---- Functions ----
        //! Called every frame to declare the pass's resources and Scopes; declaring nothing
        //! skips the pass that frame.
        RenderPassBuilder& Build(eastl::function<void(RenderPassScopes&)> fn)
        {
            m_buildFunction = [fn = eastl::move(fn)](RenderGraphBuilder& builder)
            {
                RenderPassScopes scopes(builder);
                fn(scopes);
            };
            return *this;
        }

        //! Opaque work, recorded by `fn` instead of the executer submitting the Scope's items:
        //! called per view segment of each Scope, after the pass's PSO and bindings are set if
        //! it has shaders.
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

            // A pass that sets no shader has no pipeline: its .Execute sets all its state (UI).
            const bool hasPipeline = m_shaders.m_vertexShader || m_shaders.m_fragmentShader;
            if (!hasPipeline)
            {
                ASSERT(!m_shaders.m_geometryShader,
                    "Pass '{}': a GeometryShader needs a VertexShader.", m_name.GetCStr());
                ASSERT(m_executeFunction,
                    "Pass '{}': a pass without shaders records its work in .Execute.", m_name.GetCStr());
            }
            else
            {
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

            if (m_active)
                m_context->Add<ActivePassTag>(pass);

            m_context->Add<PassShaders>(pass, m_shaders);

            if (hasPipeline)
            {
                m_context->Add<PassPipelineState>(pass, m_pipelineState);

                // Eager build PipelineLayoutDescriptor from shader reflection so
                // user code (e.g. ShaderBindings::Init) can grab it before Compile.
                auto* factory = Service<RHI::Factory>::Get();
                ASSERT(factory, "Pass '{}': RHI::Factory service is not registered.",
                    m_name.GetCStr());
                if (auto layout = BuildPipelineLayoutFromShaders(*factory, m_shaders))
                {
                    // Auto-create the per-pass (space2) bindings now that the layout is
                    // reflected, but only when the shader actually declares that space, so no
                    // pass processor has to allocate them. RHIExecuteContext is current during
                    // SetUp.
                    const bool hasPerPassSpace = layout->FindSpaceGroupBySpaceId(kPerPassSpaceId) != nullptr;
                    m_context->Add<PassPipelineLayout>(pass, PassPipelineLayout{ eastl::move(layout) });
                    if (hasPerPassSpace)
                    {
                        CreatePassBindings(*m_context, *RHIExecuteContext::Current(), pass);
                    }
                }
            }

            // No default hook: the executer submits a hookless Scope's items itself.
            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
            funcs.m_executeFunction = eastl::move(m_executeFunction);
            m_context->Add<PassFunctions>(pass, eastl::move(funcs));

            if (m_hasCapabilities)
            {
                m_context->Add<PassCapabilities>(pass, m_capabilities);
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

        PassCapabilities        m_capabilities {};
        bool                    m_hasCapabilities   {false};

        PassShaders             m_shaders;
        PassPipelineState       m_pipelineState;

        BuildFunction           m_buildFunction;
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
