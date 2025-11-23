#pragma once

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

#include <ECS/ISystem.h>
#include <ECS/WorldContext.h>
#include <Tick/TickBus.h>
#include <HashString/HashString.h>

#include "InputCaptureSystem.h"

namespace Spark::Input
{
    class InputSystem : public ISystem,
                        public TickBus::Handler
    {
    public:
        // ISystem
        void Initialize() override;
        void ShutDown() override;

        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs, "WindowSystem"_hs, "UISystem"_hs};
        }

        HashString GetName() const override
        {
            return "InputSystem"_hs;
        }

        // TickBus
        void OnTick(WorldContext& context, float deltaTime) override;

        inline unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(InputTickOrder);
        }

    private:
        eastl::unique_ptr<InputCaptureSystem> m_capturer;
    };
}