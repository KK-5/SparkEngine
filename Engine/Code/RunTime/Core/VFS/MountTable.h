#pragma once

#include <shared_mutex>

#include "FileSystem.h"

namespace Spark
{
    //! A plain object that does not register itself with Service -- a test constructs one
    //! locally and passes it as a const FileSystem*, touching no global state. VFSSystem
    //! owns and registers the production instance.
    //!
    //! Mount / Unmount take the exclusive lock, everything else the shared one. ToPhysical
    //! runs on the asset worker thread and is a short lookup, so no snapshot is needed.
    class MountTable final : public FileSystem
    {
    public:
        void Mount(eastl::string_view name, eastl::string_view physicalDir) override;
        void Unmount(eastl::string_view name) override;

        eastl::string ToVirtual(eastl::string_view physicalPath) const override;
        eastl::string ToPhysical(eastl::string_view virtualPath) const override;

        eastl::vector<eastl::string> GetMountNames() const override;
        eastl::vector<eastl::string> GetPhysicalDirs() const override;

        void ListDirectory(
            eastl::string_view virtualDir,
            eastl::function<void(eastl::string_view virtualPath, bool isDirectory)> visit)
            const override;

        bool      ReadFile(eastl::string_view virtualPath,
                           eastl::vector<uint8_t>& out) const override;
        bool      WriteFile(eastl::string_view virtualPath,
                            const uint8_t* data, size_t size) const override;
        bool      Exists(eastl::string_view virtualPath) const override;
        FileStamp GetFileStamp(eastl::string_view virtualPath) const override;

    private:
        struct Entry
        {
            eastl::string m_name;
            eastl::string m_physicalDir;   ///< absolute, forward slashes, no trailing slash
        };

        // Both require the caller to hold the lock.
        const Entry*  FindUnlocked(eastl::string_view name) const;
        eastl::string DescribeUnlocked() const;

        mutable std::shared_mutex m_mutex;

        //! Registration order is load-bearing: GetPhysicalDirs feeds an ordered search.
        eastl::vector<Entry> m_entries;
    };
}
