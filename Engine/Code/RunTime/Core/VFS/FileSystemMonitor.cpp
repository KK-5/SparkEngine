#include "FileSystemMonitor.h"

#include <EASTL/algorithm.h>
#include <EASTL/chrono.h>
#include <EASTL/unique_ptr.h>

#include <Base.h>
#include <Log/ILogSystem.h>

#include "FileEventBus.h"

#if defined(_WIN32)
    #include <Windows/RunTime/Core/VFS/DirectoryChangeReader.h>
#endif

namespace Spark
{
#if defined(_WIN32)
    namespace
    {
        using Reader = Platform::DirectoryChangeReader;
        using Action = Reader::Action;
        using Clock  = eastl::chrono::steady_clock;

        struct Pending
        {
            eastl::string     m_path;     ///< virtual
            Action            m_action;
            Clock::time_point m_due;
        };

        //! Added followed by Modified is what creating a file looks like from here, and the
        //! writes belong to the creation. Everything else takes the later observation.
        Action Merge(Action existing, Action fresh)
        {
            if (existing == Action::Added && fresh == Action::Modified)
            {
                return Action::Added;
            }
            return fresh;
        }

        void Queue(const Pending& entry)
        {
            switch (entry.m_action)
            {
            case Action::Added:
                FileEventBus::QueueBroadcast(&FileEventTraits::OnFileAdded, entry.m_path);
                break;
            case Action::Removed:
                FileEventBus::QueueBroadcast(&FileEventTraits::OnFileRemoved, entry.m_path);
                break;
            case Action::Modified:
                FileEventBus::QueueBroadcast(&FileEventTraits::OnFileModified, entry.m_path);
                break;
            }
        }
    }
#endif

    FileSystemMonitor::~FileSystemMonitor()
    {
        Stop();
    }

    void FileSystemMonitor::Watch(eastl::string_view mount, eastl::string_view physicalDir)
    {
        if (mount.empty() || physicalDir.empty())
        {
            return;
        }

        for (const WatchEntry& entry: m_watches)
        {
            if (entry.m_mount == mount)
            {
                return;
            }
        }

        m_watches.push_back(WatchEntry{eastl::string(mount), eastl::string(physicalDir)});
        Restart();
    }

    void FileSystemMonitor::Unwatch(eastl::string_view mount)
    {
        const size_t before = m_watches.size();
        m_watches.erase(
            eastl::remove_if(m_watches.begin(), m_watches.end(),
                             [&](const WatchEntry& e) { return e.m_mount == mount; }),
            m_watches.end());

        if (m_watches.size() != before)
        {
            Restart();
        }
    }

#if !defined(_WIN32)

    void FileSystemMonitor::Restart() {}
    void FileSystemMonitor::Run() {}
    void FileSystemMonitor::Stop() {}

#else

    void FileSystemMonitor::Stop()
    {
        if (!m_thread.joinable())
        {
            return;
        }

        SetEvent(static_cast<HANDLE>(m_stopSignal));
        m_thread.join();

        CloseHandle(static_cast<HANDLE>(m_stopSignal));
        m_stopSignal = nullptr;
    }

    void FileSystemMonitor::Restart()
    {
        Stop();

        if (m_watches.empty())
        {
            return;
        }

        // Manual reset: the thread must stay woken until it has left the loop.
        m_stopSignal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stopSignal)
        {
            LOG_ERROR("[FileSystemMonitor] Cannot create the stop event; not watching.");
            return;
        }

        m_thread = std::thread([this] { Run(); });
    }

    void FileSystemMonitor::Run()
    {
        eastl::vector<UniquePtr<Reader>> readers;
        eastl::vector<eastl::string>     mounts;
        eastl::vector<HANDLE>            handles;

        // Index 0: WaitForMultipleObjects reports the LOWEST signalled index, so a stop
        // asked for during a burst of changes is taken before the burst drains.
        handles.push_back(static_cast<HANDLE>(m_stopSignal));

        for (const WatchEntry& entry: m_watches)
        {
            auto reader = eastl::make_unique<Reader>();
            if (!reader->Open(entry.m_physicalDir.c_str()))
            {
                LOG_WARN("[FileSystemMonitor] Cannot watch {} ({})",
                         entry.m_mount.c_str(), entry.m_physicalDir.c_str());
                continue;
            }
            handles.push_back(reader->GetEventHandle());
            mounts.push_back(entry.m_mount);
            readers.push_back(eastl::move(reader));
        }

        if (readers.empty())
        {
            return;
        }

        eastl::vector<Pending>        pending;
        eastl::vector<Reader::Change> changes;

        const auto flush = [&pending](Clock::time_point now)
        {
            for (size_t i = 0; i < pending.size();)
            {
                if (pending[i].m_due <= now)
                {
                    Queue(pending[i]);
                    pending.erase(pending.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
        };

        for (;;)
        {
            DWORD timeout = INFINITE;
            if (!pending.empty())
            {
                Clock::time_point soonest = pending.front().m_due;
                for (const Pending& entry: pending)
                {
                    soonest = eastl::min(soonest, entry.m_due);
                }
                const auto remaining = eastl::chrono::duration_cast<eastl::chrono::milliseconds>(
                                           soonest - Clock::now()).count();
                timeout = remaining > 0 ? static_cast<DWORD>(remaining) : 0;
            }

            const DWORD count  = static_cast<DWORD>(handles.size());
            const DWORD result = WaitForMultipleObjects(count, handles.data(), FALSE, timeout);

            if (result == WAIT_OBJECT_0)
            {
                break;
            }

            if (result > WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count)
            {
                const size_t index = static_cast<size_t>(result - WAIT_OBJECT_0) - 1;

                changes.clear();
                bool overflowed = false;
                if (!readers[index]->Collect(changes, overflowed))
                {
                    LOG_WARN("[FileSystemMonitor] Watch on {} stopped reporting.",
                             mounts[index].c_str());
                }

                if (overflowed)
                {
                    // A rescan covers what was pending too.
                    pending.clear();
                    FileEventBus::QueueBroadcast(&FileEventTraits::OnFileWatchOverflow);
                    continue;
                }

                const Clock::time_point due = Clock::now() + eastl::chrono::milliseconds(kDebounceMs);
                for (Reader::Change& change: changes)
                {
                    eastl::string path = mounts[index];
                    path += "://";
                    path += change.m_path;

                    auto it = eastl::find_if(pending.begin(), pending.end(),
                                             [&](const Pending& e) { return e.m_path == path; });
                    if (it != pending.end())
                    {
                        it->m_action = Merge(it->m_action, change.m_action);
                        it->m_due    = due;
                    }
                    else
                    {
                        pending.push_back(Pending{eastl::move(path), change.m_action, due});
                    }
                }
            }

            flush(Clock::now());
        }
    }

#endif
}
