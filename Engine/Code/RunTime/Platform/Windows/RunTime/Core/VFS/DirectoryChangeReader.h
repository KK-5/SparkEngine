#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Spark::Platform
{
    //! ReadDirectoryChangesW over one directory tree, as (relative path, action).
    //!
    //! Usage: Open, then wait on GetEventHandle(), then Collect -- which also arms the
    //! next read.
    class DirectoryChangeReader final
    {
    public:
        enum class Action
        {
            Added,
            Modified,
            Removed,
        };

        struct Change
        {
            eastl::string m_path;     ///< relative to the watched directory, '/'-separated
            Action        m_action;
        };

        DirectoryChangeReader() = default;
        ~DirectoryChangeReader() { Close(); }

        DirectoryChangeReader(const DirectoryChangeReader&)            = delete;
        DirectoryChangeReader& operator=(const DirectoryChangeReader&) = delete;

        bool Open(const char* directory)
        {
            Close();

            const int wide = MultiByteToWideChar(CP_UTF8, 0, directory, -1, nullptr, 0);
            if (wide <= 0)
            {
                return false;
            }
            eastl::vector<wchar_t> path(static_cast<size_t>(wide));
            MultiByteToWideChar(CP_UTF8, 0, directory, -1, path.data(), wide);

            // BACKUP_SEMANTICS is what makes CreateFile accept a directory at all.
            m_directory = CreateFileW(
                path.data(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

            if (m_directory == INVALID_HANDLE_VALUE)
            {
                m_directory = nullptr;
                return false;
            }

            m_overlapped        = OVERLAPPED{};
            m_overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!m_overlapped.hEvent)
            {
                Close();
                return false;
            }

            m_buffer.resize(kBufferBytes);
            return Arm();
        }

        void Close()
        {
            if (m_directory)
            {
                // The kernel holds m_overlapped and m_buffer while a read is armed, so the
                // cancel must be WAITED for -- returning early leaves the driver writing
                // into memory that is about to go away.
                CancelIoEx(m_directory, &m_overlapped);
                DWORD transferred = 0;
                GetOverlappedResult(m_directory, &m_overlapped, &transferred, TRUE);
                CloseHandle(m_directory);
                m_directory = nullptr;
            }
            if (m_overlapped.hEvent)
            {
                CloseHandle(m_overlapped.hEvent);
                m_overlapped.hEvent = nullptr;
            }
            m_armed = false;
        }

        bool IsOpen() const { return m_directory != nullptr; }

        HANDLE GetEventHandle() const { return m_overlapped.hEvent; }

        //! Takes the completed read's contents and arms the next one. `overflowed` means the
        //! kernel dropped changes it cannot name; the caller's only recourse is a rescan.
        bool Collect(eastl::vector<Change>& out, bool& overflowed)
        {
            overflowed = false;
            if (!m_directory || !m_armed)
            {
                return false;
            }

            DWORD transferred = 0;
            if (!GetOverlappedResult(m_directory, &m_overlapped, &transferred, FALSE))
            {
                m_armed = false;
                return false;
            }
            m_armed = false;
            ResetEvent(m_overlapped.hEvent);

            if (transferred == 0)
            {
                overflowed = true;
            }
            else
            {
                Parse(transferred, out);
            }

            return Arm();
        }

    private:
        static constexpr size_t kBufferBytes = 16 * 1024;

        bool Arm()
        {
            if (!m_directory)
            {
                return false;
            }

            ResetEvent(m_overlapped.hEvent);
            const BOOL ok = ReadDirectoryChangesW(
                m_directory, m_buffer.data(), static_cast<DWORD>(m_buffer.size()),
                TRUE,   // subtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                    | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &m_overlapped, nullptr);

            m_armed = (ok != FALSE);
            return m_armed;
        }

        void Parse(DWORD bytes, eastl::vector<Change>& out) const
        {
            const uint8_t* cursor = m_buffer.data();
            const uint8_t* end    = cursor + bytes;

            while (cursor + sizeof(FILE_NOTIFY_INFORMATION) <= end)
            {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);

                // FileName is UTF-16, NOT null terminated, and FileNameLength counts BYTES.
                const int chars = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
                if (chars > 0)
                {
                    const int utf8 = WideCharToMultiByte(CP_UTF8, 0, info->FileName, chars,
                                                         nullptr, 0, nullptr, nullptr);
                    if (utf8 > 0)
                    {
                        eastl::string path(static_cast<size_t>(utf8), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, info->FileName, chars,
                                            path.data(), utf8, nullptr, nullptr);
                        for (char& c: path)
                        {
                            if (c == '\\')
                            {
                                c = '/';
                            }
                        }

                        Change change;
                        change.m_path   = eastl::move(path);
                        change.m_action = ToAction(info->Action);
                        out.push_back(eastl::move(change));
                    }
                }

                if (info->NextEntryOffset == 0)
                {
                    break;
                }
                cursor += info->NextEntryOffset;
            }
        }

        //! A rename counts as its two halves. FileSystem::WriteFile is a temporary plus a
        //! rename, so engine writes arrive as RENAMED_NEW_NAME rather than ADDED.
        static Action ToAction(DWORD action)
        {
            switch (action)
            {
            case FILE_ACTION_ADDED:
            case FILE_ACTION_RENAMED_NEW_NAME:
                return Action::Added;
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME:
                return Action::Removed;
            default:
                return Action::Modified;
            }
        }

        HANDLE                 m_directory{nullptr};
        OVERLAPPED             m_overlapped{};
        eastl::vector<uint8_t> m_buffer;
        bool                   m_armed{false};
    };
}
