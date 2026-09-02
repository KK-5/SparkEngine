#pragma once

#include <mutex>

#include <EASTL/string.h>

#include <EBus/EBus.h>

namespace Spark
{
    //! What FileSystemMonitor sees happen to the files under the mounted directories.
    //!
    //! Queued from the monitor's thread, delivered on the main thread by
    //! ExecuteQueuedEvents() in SparkEngine::Run. Paths are virtual and passed by value —
    //! a queued event outlives the buffer its path was parsed from.
    struct FileEventTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::Single;

        static constexpr bool EnableEventQueue = true;

        using MutexType = std::mutex;

        virtual void OnFileAdded(eastl::string virtualPath) {}
        virtual void OnFileModified(eastl::string virtualPath) {}
        virtual void OnFileRemoved(eastl::string virtualPath) {}

        //! More changed at once than the kernel could hold, and nothing says which files.
        //! Anyone keeping a picture of the filesystem has to rebuild it from scratch.
        virtual void OnFileWatchOverflow() {}
    };

    using FileEventBus = EBus<FileEventTraits>;
}
