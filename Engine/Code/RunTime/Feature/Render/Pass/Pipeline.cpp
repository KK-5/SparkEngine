#include "Pipeline.h"

namespace Spark::Render
{
    Pipeline::Pipeline(eastl::string_view name)
        : m_name{ name }
    {
    }

    void Pipeline::SetName(eastl::string_view name)
    {
        m_name = name;
    }

    ObjectName Pipeline::GetName() const
    {
        return m_name;
    }

    PassContext& Pipeline::GetPassContext()
    {
        return m_passContext;
    }

    const PassContext& Pipeline::GetPassContext() const
    {
        return m_passContext;
    }
}
