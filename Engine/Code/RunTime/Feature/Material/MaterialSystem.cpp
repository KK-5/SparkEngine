#include "MaterialSystem.h"

#include "MaterialUtils.h"

namespace Spark::Material
{
    void MaterialSystem::InitInternal()
    {
        MaterialExecuteContext::Push(m_context);

        m_defaultMaterial = CreateMaterial(m_context, Resource::StandardPBR{});
        m_context.Add<DefaultMaterialTag>(m_defaultMaterial);

        // GPU texture production is part of the material system (mirrors MeshSystem
        // owning its VB/IB production). Owned + driven here; render only consumes.
        m_textureSystem.Init();

        TickBus::Handler::BusConnect();
    }

    void MaterialSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
        m_textureSystem.Shutdown();
        MaterialExecuteContext::Pop();
    }

    void MaterialSystem::OnTick(float /*deltaTime*/)
    {
        m_textureSystem.Update();
        m_textureSystem.CollectGarbage();
    }
}
