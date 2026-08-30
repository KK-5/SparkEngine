#pragma once

#include <ECS/ISystem.h>
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
    //! User materials come from Resolve() / CreateMaterial(); this system exposes no
    //! create/update API on purpose, keeping materials data-driven (the registry is
    //! the container, every system accesses it equally). Symmetric to how the engine
    //! owns the WorldContext and systems merely consume it.
    class MaterialSystem final : public ISystem,
                                 public TickBus::Handler
    {
    public:
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "MaterialSystem"; }

        //! The resident fallback material. Always valid for the system's lifetime.
        MaterialHandle GetDefaultMaterial() const { return m_defaultMaterial; }

        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(TickOrder::TICK_PRE_RENDER);
        }

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        MaterialContext       m_context;
        MaterialTextureSystem m_textureSystem;
        MaterialHandle        m_defaultMaterial{NullMaterial};
    };
}
