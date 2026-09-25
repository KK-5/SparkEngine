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
#include <RenderGraph/PassScopes.h>
#include <Resource/Shader/ShaderBuilder.h>

namespace Spark::Render
{
    //! Build a PipelineLayoutDescriptor from a PassShaders set via shader
    //! reflection. Returns nullptr if no shader is present (a pass without a
    //! pipeline). The result is owned by the caller; PassBuilder stores it on
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
        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(
            eastl::span<const Resource::ShaderAsset* const>(assets, 4));

        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            return nullptr;
        }

        auto layout = factory.CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list);
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
        RHI::MultisampleState      m_multisampleState {};
    };
    



    //! Allocate the pass's own per-pass (space2) ShaderBindings against its reflected layout
    //! and record it on the pass as PassBindings. The pass builders' Finalize calls this when
    //! the layout declares the space. The entity is created with ShaderBindingsUpdateTag so
    //! the first compile sweep produces valid GPU bindings before the first execute, and with
    //! PassShaderBindingsTag so teardown can reap it. It owns the binding's lifetime
    //! (Components::ShaderBindings holds the Ptr).
    //!
    //! Returns NullHandle, leaving the pass without PassBindings, if the pass has no
    //! PassPipelineLayout or if RHI services / Init fail. Defined in PassAccess.cpp.
    RHIHandle CreatePassBindings(PassContext& passCtx, RHIContext& rhiCtx, Pass pass);

    // RenderPassBuilder<PassTag> + SPARK_RENDER_PASS live in <Pass/RenderPass.h>.
    // It pulls PassCapabilities.h, kept out of this common header so only files that write
    // render passes pay for it.

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
        ComputePassBuilder& Build(BuildFunction fn)
        {
            m_buildFunction = eastl::move(fn);
            return *this;
        }

        //! Build through ComputePassScopes: the pass opens its Scopes itself.
        ComputePassBuilder& BuildScopes(eastl::function<void(ComputePassScopes&)> fn)
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
            ASSERT(m_executeFunction, "Compute pass '{}': Execute function is required.", m_name.GetCStr());

            // A pass that sets no shader has no pipeline: its .Execute sets all its state.
            const bool hasPipeline = m_shaders.m_computeShader != nullptr;

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
