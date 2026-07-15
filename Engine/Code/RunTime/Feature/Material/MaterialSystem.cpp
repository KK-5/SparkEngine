#include "MaterialSystem.h"

#include "MaterialUtils.h"

namespace Spark::Material
{
    void MaterialSystem::InitInternal()
    {
        // Push the store first so any consumer (later phases: render sync, editor)
        // reaches it via MaterialExecuteContext::Current(). Material's ExecuteContext
        // stack is keyed on MaterialHandle, independent of the world/RHI stacks.
        MaterialExecuteContext::Push(m_context);

        // Default params reproduce today's hardcoded GBuffer look (see MaterialParams
        // defaults). Resident for the system's lifetime — the fallback that keeps
        // "material deleted / unset" from ever breaking a draw.
        m_defaultMaterial = CreateMaterial(m_context, MaterialParams{});
        m_context.Add<DefaultMaterialTag>(m_defaultMaterial);
    }

    void MaterialSystem::ShutdownInternal()
    {
        MaterialExecuteContext::Pop();
        // m_context clears itself on destruction (BasicContext dtor).
    }
}
