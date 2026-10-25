#pragma once

#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    // ================================================================
    // CopyPassBuilder<PassTag>
    // ================================================================
    //! A pass of copies only: no shader, no bindings, its Scopes copy between the buffers they
    //! access.
    template<typename PassTag>
    class CopyPassBuilder
    {
    public:
        CopyPassBuilder& Queue(RHI::HardwareQueueClass q)
        {
            m_queue = q;
            m_queueSet = true;
            return *this;
        }

        CopyPassBuilder& Inactive()
        {
            m_active = false;
            return *this;
        }

        //! Called every frame to declare the pass's resources and Scopes; declaring nothing
        //! skips the pass that frame.
        CopyPassBuilder& Build(eastl::function<void(CopyPassScopes&)> fn)
        {
            m_buildFunction = [fn = eastl::move(fn)](RenderGraphBuilder& builder)
            {
                CopyPassScopes scopes(builder);
                fn(scopes);
            };
            return *this;
        }

        Pass Finalize()
        {
            ASSERT(!m_finalized, "Copy pass '{}' is already finalized.", m_name.GetCStr());
            ASSERT(m_queueSet, "Copy pass '{}': Queue must be set.", m_name.GetCStr());
            ASSERT(m_buildFunction, "Copy pass '{}': Build function is required.", m_name.GetCStr());

            Pass pass = m_context->CreatePass();

            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Copy pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<CopyPassTag>(pass);
            m_context->Add<PassExecuteQueue>(pass, PassExecuteQueue{m_queue});

            if (m_active)
            {
                m_context->Add<ActivePassTag>(pass);
            }

            PassFunctions funcs;
            funcs.m_buildFunction = eastl::move(m_buildFunction);
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

        PassContext*                               m_context;
        ObjectName                                 m_name;
        RHI::HardwareQueueClass                    m_queue {};
        bool                                       m_active    {true};
        eastl::function<void(RenderGraphBuilder&)> m_buildFunction;
        bool                                       m_queueSet  {false};
        bool                                       m_finalized {false};
    };

    // ================================================================
    // Factory functions
    // ================================================================
    template<typename PassTag>
    CopyPassBuilder<PassTag> RegisterCopyPass(PassContext& ctx, ObjectName name)
    {
        return CopyPassBuilder<PassTag>(ctx, name);
    }
} // namespace Spark::Render

#define SPARK_COPY_PASS(ctx, NAME) \
    ::Spark::Render::RegisterCopyPass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))
