#include "MaterialSystem.h"

#include <cstdint>

#include <EASTL/vector.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>

#include "MaterialUtils.h"

#include <Resource/AssetManagerInterface.h>

namespace Spark::Material
{
    struct MaterialLiveMark
    {
        uint64_t m_gen = 0;
    };

    void MaterialSystem::InitInternal()
    {
        MaterialExecuteContext::Push(m_context);

        Resource::StandardPBR defaultParam{};
        m_defaultMaterial = CreateMaterial(m_context, defaultParam);
        m_context.Add<DefaultMaterialTag>(m_defaultMaterial);

        ComponentEventBus::Handler::BusConnect(GetTypeId<MaterialComponent>());

        // GPU texture production is part of the material system (mirrors MeshSystem
        // owning its VB/IB production). Owned + driven here; render only consumes.
        m_textureSystem.Init();

        // Drive the texture producer + garbage collectors (see OnTick).
        TickBus::Handler::BusConnect();
    }

    void MaterialSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
        m_textureSystem.Shutdown();
        ComponentEventBus::Handler::BusDisconnect();
        MaterialExecuteContext::Pop();
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

        Resource::StandardPBR params = m_context.Has<Resource::StandardPBR>(m_defaultMaterial)
            ? m_context.Get<Resource::StandardPBR>(m_defaultMaterial)
            : Resource::StandardPBR{};
        mc->m_material = CreateMaterial(m_context, params);
    }

    void MaterialSystem::OnTick(float /*deltaTime*/)
    {
        m_textureSystem.Update();
        m_textureSystem.CollectGarbage();
        CollectGarbage();
    }

    void MaterialSystem::CollectGarbage()
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        ++m_gcGeneration;

        auto rootView = world->GetView<MaterialComponent>();
        for (auto entity : rootView)
        {
            const MaterialHandle handle = world->Get<MaterialComponent>(entity).m_material;
            if (m_context.Valid(handle))
            {
                m_context.AddOrReplace<MaterialLiveMark>(handle, m_gcGeneration);
            }
        }
        if (m_context.Valid(m_defaultMaterial))
        {
            m_context.AddOrReplace<MaterialLiveMark>(m_defaultMaterial, m_gcGeneration);
        }

        eastl::vector<MaterialHandle> dead;
        auto matView = m_context.GetView<Resource::StandardPBR>();
        for (auto entity : matView)
        {
            const MaterialLiveMark* mark = m_context.TryGet<MaterialLiveMark>(entity);
            if (mark == nullptr || mark->m_gen != m_gcGeneration)
            {
                dead.push_back(entity);
            }
        }
        if (!dead.empty())
        {
            m_context.DestoryEntity(dead.begin(), dead.end());
        }
    }
}
