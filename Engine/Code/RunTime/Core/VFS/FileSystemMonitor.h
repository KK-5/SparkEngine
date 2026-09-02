#pragma once

#include <thread>

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Spark
{
    //! Watches the mounted directories and turns what it sees into FileEventBus events.
    //!
    //! A path is reported once it has been quiet for kDebounceMs. One save produces several
    //! notifications, and the delay also keeps a file that is still being written from
    //! being read half-finished.
    class FileSystemMonitor final
    {
    public:
        FileSystemMonitor() = default;
        ~FileSystemMonitor();

        FileSystemMonitor(const FileSystemMonitor&)            = delete;
        FileSystemMonitor& operator=(const FileSystemMonitor&) = delete;

        //! `mount` is carried so the thread can build virtual paths without calling back
        //! into FileSystem. Restarts the thread.
        void Watch(eastl::string_view mount, eastl::string_view physicalDir);

        void Unwatch(eastl::string_view mount);

        void Stop();

    private:
        struct WatchEntry
        {
            eastl::string m_mount;
            eastl::string m_physicalDir;
        };

        void Restart();
        void Run();

        static constexpr int64_t kDebounceMs = 200;

        eastl::vector<WatchEntry> m_watches;
        std::thread               m_thread;

        //! void* rather than HANDLE, to keep <Windows.h> out of everything that mounts.
        void*                     m_stopSignal{nullptr};
    };
}
