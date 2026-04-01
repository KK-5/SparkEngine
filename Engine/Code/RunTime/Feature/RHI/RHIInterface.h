#pragma once

#include <ECS/ISystem.h>

#include "Base.h"
#include "Factory.h"
#include "Bus/FrameEventBus.h"

namespace Spark::RHI
{
    class RHIInterface : public ISystem
                       , public FrameEventBus::Handler
    {
    public:
        virtual ~RHIInterface() = default;

        virtual BackendType GetBackendType() const = 0;

        virtual Factory* GetRHIFactory() const = 0;

        ///////////////////////////////
        eastl::vector<HashString> Request() const override
        {
            return {};
        }

        HashString GetName() const override
        {
            return "RHI"_hs;
        }
    };
}