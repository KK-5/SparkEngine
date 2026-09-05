#pragma once

#include <ECS/ISystem.h>
#include <Service/Service.h>

#include "FileSystemMonitor.h"
#include "MountTable.h"

namespace Spark
{
    //! Lifetime owner and Service registration point for the FileSystem; forwards to an
    //! internal MountTable.
    //!
    //! Created right after SpdLogSystem (MountTable logs its errors) and before
    //! AssetManager, since mounts must be in place before any AssetId is produced.
    //! Registration happens in the Service<FileSystem>::Handler constructor -- at
    //! CreateSystem, ahead of Init().
    class VFSSystem final : public ISystem, public Service<FileSystem>::Handler
    {
    public:
        eastl::vector<HashString> Request() const override { return {"LogSystem"_hs}; }
        HashString                GetName() const override { return "VFS"_hs; }

        void Mount(eastl::string_view name, eastl::string_view physicalDir) override
        {
            Mount(name, physicalDir, true);
        }

        //! `watch` false leaves the tree unmonitored -- for one holding generated output,
        //! where every event would be our own writes coming back.
        void Mount(eastl::string_view name, eastl::string_view physicalDir, bool watch)
        {
            m_table.Mount(name, physicalDir);
            if (watch)
            {
                m_monitor.Watch(name, physicalDir);
            }
        }

        void Unmount(eastl::string_view name) override
        {
            m_monitor.Unwatch(name);
            m_table.Unmount(name);
        }

        eastl::string ToVirtual(eastl::string_view physicalPath) const override
        {
            return m_table.ToVirtual(physicalPath);
        }

        eastl::string ToPhysical(eastl::string_view virtualPath) const override
        {
            return m_table.ToPhysical(virtualPath);
        }

        eastl::vector<eastl::string> GetMountNames() const override
        {
            return m_table.GetMountNames();
        }

        eastl::vector<eastl::string> GetPhysicalDirs() const override
        {
            return m_table.GetPhysicalDirs();
        }

        void ListDirectory(
            eastl::string_view virtualDir,
            eastl::function<void(eastl::string_view virtualPath, bool isDirectory)> visit)
            const override
        {
            m_table.ListDirectory(virtualDir, eastl::move(visit));
        }

        bool ReadFile(eastl::string_view virtualPath, eastl::vector<uint8_t>& out) const override
        {
            return m_table.ReadFile(virtualPath, out);
        }

        bool WriteFile(eastl::string_view virtualPath,
                       const uint8_t* data, size_t size) const override
        {
            return m_table.WriteFile(virtualPath, data, size);
        }

        bool Exists(eastl::string_view virtualPath) const override
        {
            return m_table.Exists(virtualPath);
        }

        FileStamp GetFileStamp(eastl::string_view virtualPath) const override
        {
            return m_table.GetFileStamp(virtualPath);
        }

    private:
        void InitInternal() override {}

        void ShutdownInternal() override { m_monitor.Stop(); }

        MountTable        m_table;
        FileSystemMonitor m_monitor;
    };
}
