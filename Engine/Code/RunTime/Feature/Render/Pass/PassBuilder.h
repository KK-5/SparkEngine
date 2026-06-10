#pragma once

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <Base.h>

#include <RHI/Factory.h>
#include <RHI/Format.h>
#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/RenderStates.h>

#include <Pass/Pass.h>
#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/Component/PassComponents.h>
#include <Shader/ShaderBuilder.h>

namespace Spark::Render
{
    //! Build a PipelineLayoutDescriptor from a PassShaders set via shader
    //! reflection. Returns nullptr if no shader is present (custom-pipeline
    //! passes). The result is owned by the caller; PassBuilder stores it on
    //! the pass as PassPipelineLayout so user Build callbacks can grab it
    //! before the render-graph Compile phase runs.
    inline Ptr<RHI::PipelineLayoutDescriptor> BuildPipelineLayoutFromShaders(
        RHI::Factory& factory, const PassShaders& shaders)
    {
        const Resource::ShaderAsset* assets[] = {
            shaders.m_vertexShader.get(),
            shaders.m_fragmentShader.get(),
            shaders.m_geometryShader.get(),
            shaders.m_computeShader.get(),
        };
        ShaderInputBuildResult built = BuildShaderInputList(
            eastl::span<const Resource::ShaderAsset* const>(assets, 4));

        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            return nullptr;
        }

        auto layout = factory.CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();
        return layout;
    }

    struct RenderPassConfig
    {
        Ptr<Resource::ShaderAsset> m_vertexShader = nullptr;
        Ptr<Resource::ShaderAsset> m_fragmentShader = nullptr;
        Ptr<Resource::ShaderAsset> m_geometryShader = nullptr;
        RHI::InputStreamLayout     m_inputLayout {};
        RHI::RenderStates          m_renderStates {};
        RHI::RenderTargetLayout    m_renderTargetLayout {};
        RHI::Viewport              m_viewport {};
        RHI::Scissor               m_scissor {};
        RHI::MultisampleState      m_multisampleState {};
    };
    


    // ================================================================
    // RenderPassBuilder<PassTag> — chainable builder for graphics passes
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
            ASSERT(m_executeFunction, "Pass '{}': Execute function is required.", m_name.GetCStr());

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

            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
            funcs.m_compileFunction = eastl::move(m_compileFunction);
            funcs.m_executeFunction = eastl::move(m_executeFunction);
            m_context->Add<PassFunctions>(pass, eastl::move(funcs));

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

    // ================================================================
    // ComputePassBuilder<PassTag>
    // ================================================================
    template<typename PassTag>
    class ComputePassBuilder
    {
    public:
        using BuildFunction   = eastl::function<void(RenderGraphBuilder&)>;
        using CompileFunction = eastl::function<void(RenderGraphCompiler&)>;
        using ExecuteFunction = eastl::function<void(ExecuteWork&, RenderGraphExecuter&)>;

        ComputePassBuilder& Queue(RHI::HardwareQueueClass q)
        {
            m_queue = q;
            m_queueSet = true;
            return *this;
        }

        ComputePassBuilder& Inactive()
        {
            m_active = false;
            return *this;
        }

        // ---- Shader ----
        ComputePassBuilder& ComputeShader(Ptr<Resource::ShaderAsset> asset)
        {
            m_shaders.m_computeShader = eastl::move(asset);
            return *this;
        }

        // ---- Custom pipeline ----
        ComputePassBuilder& CustomPipeline()
        {
            m_customPipeline = true;
            return *this;
        }

        // ---- Functions ----
        ComputePassBuilder& Build(BuildFunction fn)
        {
            m_buildFunction = eastl::move(fn);
            return *this;
        }

        ComputePassBuilder& Compile(CompileFunction fn)
        {
            m_compileFunction = eastl::move(fn);
            return *this;
        }

        ComputePassBuilder& Execute(ExecuteFunction fn)
        {
            m_executeFunction = eastl::move(fn);
            return *this;
        }

        // ---- Finalize ----
        Pass Finalize()
        {
            ASSERT(!m_finalized, "Compute pass '{}' is already finalized.", m_name.GetCStr());
            ASSERT(m_queueSet, "Compute pass '{}': Queue must be set.", m_name.GetCStr());
            ASSERT(m_buildFunction, "Compute pass '{}': Build function is required.", m_name.GetCStr());
            ASSERT(m_executeFunction, "Compute pass '{}': Execute function is required.", m_name.GetCStr());

            if (m_customPipeline)
            {
                ASSERT(!m_shaders.m_computeShader,
                    "Compute pass '{}': CustomPipeline pass must not set shaders.", m_name.GetCStr());
            }
            else
            {
                ASSERT(m_shaders.m_computeShader,
                    "Compute pass '{}': ComputePass needs ComputeShader.", m_name.GetCStr());
            }

            Pass pass = m_context->CreatePass();

            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Compute pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<ComputePassTag>(pass);
            m_context->Add<PassExecuteQueue>(pass, PassExecuteQueue{m_queue});
            m_context->Add<PassAttachmentMarker>(pass, MarkPassAttachmentCompiling<PassTag>());

            if (m_active)
                m_context->Add<ActivePassTag>(pass);

            m_context->Add<PassShaders>(pass, m_shaders);

            if (m_customPipeline)
            {
                m_context->Add<CustomPipelinePassTag>(pass);
            }
            else
            {
                // Eager build PipelineLayoutDescriptor from shader reflection so
                // user code (e.g. ShaderBindings::Init) can grab it before Compile.
                auto* factory = Service<RHI::Factory>::Get();
                ASSERT(factory, "Compute pass '{}': RHI::Factory service is not registered.",
                    m_name.GetCStr());
                if (auto layout = BuildPipelineLayoutFromShaders(*factory, m_shaders))
                {
                    m_context->Add<PassPipelineLayout>(pass, PassPipelineLayout{ eastl::move(layout) });
                }
            }

            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
            funcs.m_compileFunction = eastl::move(m_compileFunction);
            funcs.m_executeFunction = eastl::move(m_executeFunction);
            m_context->Add<PassFunctions>(pass, eastl::move(funcs));

            m_finalized = true;
            return pass;
        }

    private:
        template<typename T>
        friend ComputePassBuilder<T> RegisterComputePass(PassContext&, ObjectName);

        ComputePassBuilder(PassContext& ctx, ObjectName name)
            : m_context(&ctx)
            , m_name(name)
        {
        }

        PassContext*            m_context;
        ObjectName              m_name;
        RHI::HardwareQueueClass m_queue {};
        bool                    m_active            {true};
        bool                    m_customPipeline    {false};

        PassShaders             m_shaders;

        BuildFunction           m_buildFunction;
        CompileFunction         m_compileFunction;
        ExecuteFunction         m_executeFunction;

        bool                    m_queueSet  {false};
        bool                    m_finalized {false};
    };

    // ================================================================
    // CopyPassBuilder<PassTag>
    //
    // For passes that issue raw command-list copy/blit/resolve work: no
    // shaders, no PSO, no render targets. Always a custom-pipeline pass and
    // defaults to the Copy queue (override via Queue() if the copy must run on
    // Graphics/Compute). Build declares the source/dest attachments; Execute
    // resolves them (FindPassAttachmentImage) and submits the CopyItem.
    // ================================================================
    template<typename PassTag>
    class CopyPassBuilder
    {
    public:
        using BuildFunction   = eastl::function<void(RenderGraphBuilder&)>;
        using CompileFunction = eastl::function<void(RenderGraphCompiler&)>;
        using ExecuteFunction = eastl::function<void(ExecuteWork&, RenderGraphExecuter&)>;

        CopyPassBuilder& Queue(RHI::HardwareQueueClass q)
        {
            m_queue = q;
            return *this;
        }

        CopyPassBuilder& Inactive()
        {
            m_active = false;
            return *this;
        }

        // ---- Functions ----
        CopyPassBuilder& Build(BuildFunction fn)
        {
            m_buildFunction = eastl::move(fn);
            return *this;
        }

        CopyPassBuilder& Compile(CompileFunction fn)
        {
            m_compileFunction = eastl::move(fn);
            return *this;
        }

        CopyPassBuilder& Execute(ExecuteFunction fn)
        {
            m_executeFunction = eastl::move(fn);
            return *this;
        }

        // ---- Finalize ----
        Pass Finalize()
        {
            ASSERT(!m_finalized, "Copy pass '{}' is already finalized.", m_name.GetCStr());
            ASSERT(m_buildFunction, "Copy pass '{}': Build function is required.", m_name.GetCStr());
            ASSERT(m_executeFunction, "Copy pass '{}': Execute function is required.", m_name.GetCStr());

            Pass pass = m_context->CreatePass();

            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Copy pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<CopyPassTag>(pass);
            // A copy pass never owns an engine PSO; tag it so the PSO compiler skips it.
            m_context->Add<CustomPipelinePassTag>(pass);
            m_context->Add<PassExecuteQueue>(pass, PassExecuteQueue{m_queue});
            m_context->Add<PassAttachmentMarker>(pass, MarkPassAttachmentCompiling<PassTag>());

            if (m_active)
                m_context->Add<ActivePassTag>(pass);

            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
            funcs.m_compileFunction = eastl::move(m_compileFunction);
            funcs.m_executeFunction = eastl::move(m_executeFunction);
            m_context->Add<PassFunctions>(pass, eastl::move(funcs));

            m_finalized = true;
            return pass;
        }

    private:
        template<typename T>
        friend CopyPassBuilder<T> RegisterCopyPass(PassContext&, ObjectName);

        CopyPassBuilder(PassContext& ctx, ObjectName name)
            : m_context(&ctx)
            , m_name(name)
        {
        }

        PassContext*            m_context;
        ObjectName              m_name;
        RHI::HardwareQueueClass m_queue   {RHI::HardwareQueueClass::Copy};
        bool                    m_active  {true};

        BuildFunction           m_buildFunction;
        CompileFunction         m_compileFunction;
        ExecuteFunction         m_executeFunction;

        bool                    m_finalized {false};
    };

    // ================================================================
    // Factory functions
    // ================================================================
    template<typename PassTag>
    RenderPassBuilder<PassTag> RegisterRenderPass(PassContext& ctx, ObjectName name)
    {
        return RenderPassBuilder<PassTag>(ctx, name);
    }

    template<typename PassTag>
    ComputePassBuilder<PassTag> RegisterComputePass(PassContext& ctx, ObjectName name)
    {
        return ComputePassBuilder<PassTag>(ctx, name);
    }

    template<typename PassTag>
    CopyPassBuilder<PassTag> RegisterCopyPass(PassContext& ctx, ObjectName name)
    {
        return CopyPassBuilder<PassTag>(ctx, name);
    }

} // namespace Spark::Render

#define SPARK_RENDER_PASS(ctx, NAME) \
    ::Spark::Render::RegisterRenderPass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))

#define SPARK_COMPUTE_PASS(ctx, NAME) \
    ::Spark::Render::RegisterComputePass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))

#define SPARK_COPY_PASS(ctx, NAME) \
    ::Spark::Render::RegisterCopyPass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))
