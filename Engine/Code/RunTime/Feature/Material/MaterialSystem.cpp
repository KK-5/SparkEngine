#include "MaterialSystem.h"

#include "MaterialUtils.h"

namespace Spark::Material
{
    void MaterialSystem::InitInternal()
    {
        MaterialExecuteContext::Push(m_context);

        m_defaultMaterial = CreateMaterial(m_context, Resource::StandardPBR{});
        m_context.Add<DefaultMaterialTag>(m_defaultMaterial);
    }

    void MaterialSystem::ShutdownInternal()
    {
        MaterialExecuteContext::Pop();
    }
}
