#pragma once

#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    template<typename PassTag>
    class AccelerationStructurePassBuilder
    {
    public:
        AccelerationStructurePassBuilder& Queue(RHI::HardwareQueueClass q)
        {
            m_queue = q;
            m_queueSet = true;
            return *this;
        }

        AccelerationStructurePassBuilder& Inactive()
        {
            m_active = false;
            return *this;
        }

        AccelerationStructurePassBuilder& Build(eastl::function<void(AccelerationStructurePassScopes&)> fn)
        {
            m_buildFunction = [fn = eastl::move(fn)](RenderGraphBuilder& builder)
            {
                AccelerationStructurePassScopes scopes(builder);
                fn(scopes);
            };
            return *this;
        }

        Pass Finalize()
        {
            ASSERT(!m_finalized, "Acceleration structure pass '{}' is already finalized.", m_name.GetCStr());
            ASSERT(m_queueSet, "Acceleration structure pass '{}': Queue must be set.", m_name.GetCStr());
            ASSERT(m_queue == RHI::HardwareQueueClass::Graphics || m_queue == RHI::HardwareQueueClass::Compute,
                "Acceleration structure pass '{}': Queue must be Graphics or Compute.", m_name.GetCStr());
            ASSERT(m_buildFunction, "Acceleration structure pass '{}': Build function is required.", m_name.GetCStr());

            Pass pass = m_context->CreatePass();

            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Acceleration structure pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<AccelerationStructurePassTag>(pass);
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
        friend AccelerationStructurePassBuilder<T> RegisterAccelerationStructurePass(PassContext&, ObjectName);

        AccelerationStructurePassBuilder(PassContext& ctx, ObjectName name)
            : m_context(&ctx)
            , m_name(name)
        {
        }

        PassContext*                              m_context;
        ObjectName                                m_name;
        RHI::HardwareQueueClass                   m_queue {};
        bool                                      m_active {true};
        eastl::function<void(RenderGraphBuilder&)> m_buildFunction;
        bool                                      m_queueSet {false};
        bool                                      m_finalized {false};
    };

    template<typename PassTag>
    AccelerationStructurePassBuilder<PassTag> RegisterAccelerationStructurePass(PassContext& ctx, ObjectName name)
    {
        return AccelerationStructurePassBuilder<PassTag>(ctx, name);
    }
}

#define SPARK_ACCELERATION_STRUCTURE_PASS(ctx, NAME) \
    ::Spark::Render::RegisterAccelerationStructurePass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))
