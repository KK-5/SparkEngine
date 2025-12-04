#pragma once

#include <mutex>

#include <EBUS/EBus.h>

namespace Spark::Render::RHI
{
    class FrameGraph;
    class FrameEventInterface : public EBusTraits
    {
    public:
        static constexpr EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static constexpr EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

        using MutexType = std::recursive_mutex;
    public:
        virtual void OnFrameBegin() {}

        virtual void OnFrameCompileBegin() {}

        virtual void OnFrameCompileEnd([[maybe_unused]] FrameGraph& frameGraph) {}

        virtual void OnFrameEnd() {}
    };

    using FrameEventBus = EBus<FrameEventInterface>;
}