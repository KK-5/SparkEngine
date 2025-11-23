#pragma once

#include <shared_mutex>
#include <memory>

#include <EASTL/vector.h>
#include <HashString/HashString.h>

namespace Spark
{
    // Base class of all system
    class ISystem
    {
    public:
        ISystem() = default;
        virtual ~ISystem() = default;

        ISystem(const ISystem&) = delete;
        ISystem& operator=(const ISystem&) = delete;

        ISystem(ISystem &&) = default;
        ISystem& operator=(ISystem&&) = default;

        virtual void                      Initialize()     = 0;
        virtual void                      ShutDown()       = 0;
        virtual eastl::vector<HashString> Request() const  = 0;
        virtual HashString                GetName() const  = 0;
    };
}
