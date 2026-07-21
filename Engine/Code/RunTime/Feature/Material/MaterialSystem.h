#pragma once

#include <ECS/ISystem.h>
#include <ECS/Bus/ComponentEventBus.h>
#include <Tick/TickBus.h>

#include "MaterialContext.h"
#include "MaterialHandle.h"
#include "MaterialTextureSystem.h"

namespace Spark::Material
{
    //! Lifecycle owner of the MaterialContext — NOT a material factory. It owns the
    //! context, pushes MaterialExecuteContext so any code can reach the store via
    //! MaterialExecuteContext::Current(), and creates + holds the always-present
    //! default material that stale/unset MaterialComponent references fall back to.
    //!
    //! User materials are created with the free CreateMaterial(); this system exposes
    //! no create/update API on purpose, keeping materials data-driven (the registry
    //! is the container, every system accesses it equally). Symmetric to how the
    //! engine owns the WorldContext and systems merely consume it.
    class MaterialSystem final : public ISystem,
                                 public ComponentEventBus::Handler,
                                 public TickBus::Handler
    {
    public:
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "MaterialSystem"; }

        //! The resident fallback material. Always valid for the system's lifetime.
        MaterialHandle GetDefaultMaterial() const { return m_defaultMaterial; }

        void OnComponentConstruct(Entity entity) override;

        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(TickOrder::TICK_PRE_RENDER);
        }

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        void CollectGarbage();

        MaterialContext       m_context;
        MaterialTextureSystem m_textureSystem;
        MaterialHandle        m_defaultMaterial{NullMaterial};

        // Bumped each GC cycle; the epoch a material must carry to be considered live.
        uint64_t m_gcGeneration = 0;
    };
}
