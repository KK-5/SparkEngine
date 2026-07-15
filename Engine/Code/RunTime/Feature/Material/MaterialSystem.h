#pragma once

#include <ECS/ISystem.h>

#include "MaterialContext.h"
#include "MaterialHandle.h"

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
    class MaterialSystem final : public ISystem
    {
    public:
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "MaterialSystem"; }

        //! The resident fallback material. Always valid for the system's lifetime.
        MaterialHandle GetDefaultMaterial() const { return m_defaultMaterial; }

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        MaterialContext m_context;
        MaterialHandle  m_defaultMaterial{NullMaterial};
    };
}
