#include "MaterialSystem.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>

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

        // Listen for MaterialComponent adds on world entities to auto-create a private
        // material (see OnComponentConstruct).
        ComponentEventBus::Handler::BusConnect(GetTypeId<MaterialComponent>());
    }

    void MaterialSystem::ShutdownInternal()
    {
        ComponentEventBus::Handler::BusDisconnect();
        MaterialExecuteContext::Pop();
        // m_context clears itself on destruction (BasicContext dtor).
    }

    void MaterialSystem::OnComponentConstruct(Entity entity)
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        auto* mc = world->TryGet<MaterialComponent>(entity);
        if (!mc || mc->m_material != NullMaterial)
        {
            // Already carries a material (a future share / asset path set it in the
            // same Add) — leave it untouched.
            return;
        }

        // Give the object its OWN private material, seeded from the default's current
        // params so it looks unchanged the moment it is added. Editing it then affects
        // only this object (per-object material). Lifecycle: persists for now — GC /
        // destruction is deferred, so this material is never reclaimed yet.
        MaterialParams params = m_context.Has<MaterialParams>(m_defaultMaterial)
            ? m_context.Get<MaterialParams>(m_defaultMaterial)
            : MaterialParams{};
        mc->m_material = CreateMaterial(m_context, params);
    }
}
