#pragma once

#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    // ================================================================
    // ComputePassBuilder<PassTag>
    // ================================================================
    template<typename PassTag>
    class ComputePassBuilder
    {
    public:
        using BuildFunction   = eastl::function<void(RenderGraphBuilder&)>;
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

        // ---- Functions ----
        //! Called every frame to declare the pass's resources and Scopes; declaring nothing
        //! skips the pass that frame.
        ComputePassBuilder& Build(eastl::function<void(ComputePassScopes&)> fn)
        {
            m_buildFunction = [fn = eastl::move(fn)](RenderGraphBuilder& builder)
            {
                ComputePassScopes scopes(builder);
                fn(scopes);
            };
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

            // A pass that sets no shader has no pipeline: its .Execute sets all its state.
            const bool hasPipeline = m_shaders.m_computeShader != nullptr;
            ASSERT(hasPipeline || m_executeFunction,
                "Compute pass '{}' sets no shader, so its .Execute is required.", m_name.GetCStr());

            Pass pass = m_context->CreatePass();

            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Compute pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<ComputePassTag>(pass);
            m_context->Add<PassExecuteQueue>(pass, PassExecuteQueue{m_queue});

            if (m_active)
                m_context->Add<ActivePassTag>(pass);

            m_context->Add<PassShaders>(pass, m_shaders);

            if (hasPipeline)
            {
                // Eager build PipelineLayoutDescriptor from shader reflection so
                // user code (e.g. ShaderBindings::Init) can grab it before Compile.
                auto* factory = Service<RHI::Factory>::Get();
                ASSERT(factory, "Compute pass '{}': RHI::Factory service is not registered.",
                    m_name.GetCStr());
                if (auto layout = BuildPipelineLayoutFromShaders(*factory, m_shaders))
                {
                    // Auto-create the per-pass (space2) bindings, as RenderPassBuilder does.
                    const bool hasPerPassSpace = layout->FindSpaceGroupBySpaceId(kPerPassSpaceId) != nullptr;
                    m_context->Add<PassPipelineLayout>(pass, PassPipelineLayout{ eastl::move(layout) });
                    if (hasPerPassSpace)
                    {
                        CreatePassBindings(*m_context, *RHIExecuteContext::Current(), pass);
                    }
                }

                const Resource::ShaderStageReflection* reflection =
                    m_shaders.m_computeShader->GetStageReflection(RHI::ShaderStage::Compute);
                ASSERT(reflection, "Compute pass '{}': its shader has no compute stage.", m_name.GetCStr());
                m_context->Add<PassThreadGroupSize>(pass, PassThreadGroupSize{
                    static_cast<uint16_t>(reflection->m_threadGroupSizeX),
                    static_cast<uint16_t>(reflection->m_threadGroupSizeY),
                    static_cast<uint16_t>(reflection->m_threadGroupSizeZ) });
            }

            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
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

        PassShaders             m_shaders;

        BuildFunction           m_buildFunction;
        ExecuteFunction         m_executeFunction;

        bool                    m_queueSet  {false};
        bool                    m_finalized {false};
    };

    // ================================================================
    // Factory functions
    // ================================================================
    template<typename PassTag>
    ComputePassBuilder<PassTag> RegisterComputePass(PassContext& ctx, ObjectName name)
    {
        return ComputePassBuilder<PassTag>(ctx, name);
    }
} // namespace Spark::Render

#define SPARK_COMPUTE_PASS(ctx, NAME) \
    ::Spark::Render::RegisterComputePass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))
