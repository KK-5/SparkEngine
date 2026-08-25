#include "MountTable.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>

namespace Spark
{
    namespace
    {
        constexpr const char* kScheme    = "://";
        constexpr size_t      kSchemeLen = 3;

        eastl::string Str(eastl::string_view v)
        {
            return eastl::string(v.data(), v.size());
        }

        eastl::string FromStd(const std::string& s)
        {
            return eastl::string(s.c_str(), s.size());
        }

        std::string ToStd(eastl::string_view v)
        {
            return std::string(v.data(), v.size());
        }

        eastl::string NormalizeSeparators(eastl::string_view path)
        {
            eastl::string out = Str(path);
            for (char& c : out)
            {
                if (c == '\\')
                {
                    c = '/';
                }
            }
            while (out.size() > 1 && out.back() == '/')
            {
                out.pop_back();
            }
            return out;
        }

        //! Absolute + forward slashes + no trailing slash. Does not require the path to exist,
        //! so a mount can name a directory that has not been created yet.
        eastl::string CanonicalPhysical(eastl::string_view dir)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            const fs::path absolute = fs::absolute(fs::path(ToStd(dir)), ec);
            if (ec)
            {
                return NormalizeSeparators(dir);
            }
            return NormalizeSeparators(FromStd(absolute.lexically_normal().generic_string()));
        }

        char LowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        //! Windows paths (drive letter included) are case-insensitive. This applies to the
        //! physical prefix only -- the relative segment of a virtual path keeps the case it
        //! was authored with.
        bool PathEqualsCI(eastl::string_view a, eastl::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (LowerAscii(a[i]) != LowerAscii(b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        //! Must land on a directory boundary, so ".../Assets" does not match
        //! ".../AssetsOther/x.png".
        bool IsPathPrefix(eastl::string_view path, eastl::string_view prefix)
        {
            if (prefix.size() > path.size())
            {
                return false;
            }
            if (!PathEqualsCI(path.substr(0, prefix.size()), prefix))
            {
                return false;
            }
            return path.size() == prefix.size() || path[prefix.size()] == '/';
        }

        //! False when ".." walks above the mount root.
        bool NormalizeRelative(eastl::string_view relative, eastl::string& out)
        {
            eastl::vector<eastl::string_view> segments;

            size_t start = 0;
            while (true)
            {
                const size_t slash = relative.find('/', start);
                const eastl::string_view segment = (slash == eastl::string_view::npos)
                    ? relative.substr(start)
                    : relative.substr(start, slash - start);

                if (segment == "..")
                {
                    if (segments.empty())
                    {
                        return false;
                    }
                    segments.pop_back();
                }
                else if (!segment.empty() && segment != ".")
                {
                    segments.push_back(segment);
                }

                if (slash == eastl::string_view::npos)
                {
                    break;
                }
                start = slash + 1;
            }

            out.clear();
            for (size_t i = 0; i < segments.size(); ++i)
            {
                if (i != 0)
                {
                    out += '/';
                }
                out.append(segments[i].data(), segments[i].size());
            }
            return true;
        }

        bool SplitVirtual(eastl::string_view virtualPath,
                          eastl::string_view& outMount,
                          eastl::string_view& outRelative)
        {
            const size_t pos = virtualPath.find(kScheme, 0, kSchemeLen);
            if (pos == eastl::string_view::npos || pos == 0)
            {
                return false;
            }
            outMount    = virtualPath.substr(0, pos);
            outRelative = virtualPath.substr(pos + kSchemeLen);
            return true;
        }

        //! Unique per writer, so two threads writing the same target cannot truncate each
        //! other's temporary. Both then rename over the same bytes, which is harmless.
        std::filesystem::path MakeTempPath(const std::filesystem::path& target)
        {
            static std::atomic<uint64_t> counter{0};

            std::string suffix = ".";
            suffix += std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            suffix += '-';
            suffix += std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
            suffix += ".tmp";

            std::filesystem::path temp = target;
            temp += suffix;
            return temp;
        }

        eastl::string JoinPhysical(const eastl::string& dir, const eastl::string& relative)
        {
            if (relative.empty())
            {
                return dir;
            }
            eastl::string out = dir;
            out += '/';
            out += relative;
            return out;
        }
    }

    const MountTable::Entry* MountTable::FindUnlocked(eastl::string_view name) const
    {
        for (const Entry& entry : m_entries)
        {
            if (entry.m_name.size() == name.size()
                && entry.m_name.compare(0, name.size(), name.data(), name.size()) == 0)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    eastl::string MountTable::DescribeUnlocked() const
    {
        if (m_entries.empty())
        {
            return "<none>";
        }

        eastl::string out;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (i != 0)
            {
                out += ", ";
            }
            out += m_entries[i].m_name;
            out += kScheme;
            out += " -> ";
            out += m_entries[i].m_physicalDir;
        }
        return out;
    }

    void MountTable::Mount(eastl::string_view name, eastl::string_view physicalDir)
    {
        if (name.empty()
            || name.find('/') != eastl::string_view::npos
            || name.find(':') != eastl::string_view::npos)
        {
            LOG_ERROR("[MountTable] Invalid mount name '{}': must be non-empty and free of '/' and ':'.",
                Str(name).c_str());
            return;
        }

        if (physicalDir.empty())
        {
            LOG_ERROR("[MountTable] Mount '{}': physical directory is empty.", Str(name).c_str());
            return;
        }

        const eastl::string canonical = CanonicalPhysical(physicalDir);

        std::unique_lock lock(m_mutex);

        if (FindUnlocked(name))
        {
            LOG_ERROR("[MountTable] Mount name '{}' is already in use. Mounted: {}",
                Str(name).c_str(), DescribeUnlocked().c_str());
            return;
        }

        // Nesting would give one file two virtual paths, hence two AssetIds.
        for (const Entry& entry : m_entries)
        {
            if (IsPathPrefix(canonical, entry.m_physicalDir)
                || IsPathPrefix(entry.m_physicalDir, canonical))
            {
                LOG_ERROR("[MountTable] Mount '{}' -> '{}' overlaps existing '{}' -> '{}'.",
                    Str(name).c_str(), canonical.c_str(),
                    entry.m_name.c_str(), entry.m_physicalDir.c_str());
                return;
            }
        }

        m_entries.push_back(Entry{Str(name), canonical});
    }

    void MountTable::Unmount(eastl::string_view name)
    {
        std::unique_lock lock(m_mutex);

        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            if (it->m_name.size() == name.size()
                && it->m_name.compare(0, name.size(), name.data(), name.size()) == 0)
            {
                m_entries.erase(it);
                return;
            }
        }

        LOG_WARN("[MountTable] Unmount '{}': not mounted.", Str(name).c_str());
    }

    eastl::string MountTable::ToVirtual(eastl::string_view physicalPath) const
    {
        if (physicalPath.empty())
        {
            return {};
        }

        const eastl::string canonical = CanonicalPhysical(physicalPath);

        std::shared_lock lock(m_mutex);

        // Mount rejects nesting, so at most one entry can match.
        for (const Entry& entry : m_entries)
        {
            if (!IsPathPrefix(canonical, entry.m_physicalDir))
            {
                continue;
            }

            eastl::string_view relative(canonical.c_str() + entry.m_physicalDir.size(),
                                        canonical.size() - entry.m_physicalDir.size());
            while (!relative.empty() && relative.front() == '/')
            {
                relative.remove_prefix(1);
            }

            eastl::string out = entry.m_name;
            out += kScheme;
            out.append(relative.data(), relative.size());
            return out;
        }

        LOG_ERROR("[MountTable] '{}' is outside every mount point. Mounted: {}",
            canonical.c_str(), DescribeUnlocked().c_str());
        return {};
    }

    eastl::string MountTable::ToPhysical(eastl::string_view virtualPath) const
    {
        eastl::string_view mount;
        eastl::string_view relative;
        if (!SplitVirtual(virtualPath, mount, relative))
        {
            LOG_ERROR("[MountTable] '{}' is not a virtual path (expected mount://relative).",
                Str(virtualPath).c_str());
            return {};
        }

        eastl::string normalized;
        if (!NormalizeRelative(relative, normalized))
        {
            LOG_ERROR("[MountTable] '{}' escapes its mount root.", Str(virtualPath).c_str());
            return {};
        }

        std::shared_lock lock(m_mutex);

        const Entry* entry = FindUnlocked(mount);
        if (!entry)
        {
            LOG_ERROR("[MountTable] Unknown mount '{}' in '{}'. Mounted: {}",
                Str(mount).c_str(), Str(virtualPath).c_str(), DescribeUnlocked().c_str());
            return {};
        }

        return JoinPhysical(entry->m_physicalDir, normalized);
    }

    eastl::vector<eastl::string> MountTable::GetMountNames() const
    {
        std::shared_lock lock(m_mutex);

        eastl::vector<eastl::string> out;
        out.reserve(m_entries.size());
        for (const Entry& entry : m_entries)
        {
            out.push_back(entry.m_name);
        }
        return out;
    }

    eastl::vector<eastl::string> MountTable::GetPhysicalDirs() const
    {
        std::shared_lock lock(m_mutex);

        eastl::vector<eastl::string> out;
        out.reserve(m_entries.size());
        for (const Entry& entry : m_entries)
        {
            out.push_back(entry.m_physicalDir);
        }
        return out;
    }

    void MountTable::IterateDirectory(eastl::string_view virtualDir,
                                      eastl::function<void(eastl::string_view)> visit) const
    {
        namespace fs = std::filesystem;

        if (!visit)
        {
            return;
        }

        eastl::string_view mount;
        eastl::string_view relative;
        if (!SplitVirtual(virtualDir, mount, relative))
        {
            LOG_ERROR("[MountTable] '{}' is not a virtual path (expected mount://relative).",
                Str(virtualDir).c_str());
            return;
        }

        eastl::string normalized;
        if (!NormalizeRelative(relative, normalized))
        {
            LOG_ERROR("[MountTable] '{}' escapes its mount root.", Str(virtualDir).c_str());
            return;
        }

        // Hold the lock only to copy the directory out; the walk below can be slow.
        eastl::string mountDir;
        {
            std::shared_lock lock(m_mutex);

            const Entry* entry = FindUnlocked(mount);
            if (!entry)
            {
                LOG_ERROR("[MountTable] Unknown mount '{}' in '{}'. Mounted: {}",
                    Str(mount).c_str(), Str(virtualDir).c_str(), DescribeUnlocked().c_str());
                return;
            }
            mountDir = entry->m_physicalDir;
        }

        const eastl::string physicalRoot = JoinPhysical(mountDir, normalized);

        std::error_code ec;
        if (!fs::is_directory(fs::path(ToStd(physicalRoot)), ec))
        {
            LOG_WARN("[MountTable] IterateDirectory '{}': '{}' is not a directory.",
                Str(virtualDir).c_str(), physicalRoot.c_str());
            return;
        }

        // Concatenating the mount name with the walk-relative part avoids a per-file
        // ToVirtual, and with it any chance of the round trip disagreeing.
        eastl::string prefix = Str(mount);
        prefix += kScheme;

        const fs::path mountPath(ToStd(mountDir));

        for (auto it = fs::recursive_directory_iterator(fs::path(ToStd(physicalRoot)), ec),
                  end = fs::recursive_directory_iterator();
             it != end; it.increment(ec))
        {
            if (ec)
            {
                LOG_WARN("[MountTable] IterateDirectory '{}' stopped: {}",
                    Str(virtualDir).c_str(), ec.message().c_str());
                break;
            }
            if (it->is_directory(ec))
            {
                continue;
            }

            const std::string entryRelative =
                it->path().lexically_relative(mountPath).generic_string();
            if (entryRelative.empty())
            {
                continue;
            }

            eastl::string virtualPath = prefix;
            virtualPath.append(entryRelative.c_str(), entryRelative.size());
            visit(eastl::string_view(virtualPath.c_str(), virtualPath.size()));
        }
    }

    bool MountTable::ReadFile(eastl::string_view virtualPath, eastl::vector<uint8_t>& out) const
    {
        namespace fs = std::filesystem;

        const eastl::string physical = ToPhysical(virtualPath);
        if (physical.empty())
        {
            return false;
        }

        const fs::path path(ToStd(physical));

        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec)
        {
            LOG_ERROR("[MountTable] ReadFile '{}': {}", Str(virtualPath).c_str(), ec.message().c_str());
            return false;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            LOG_ERROR("[MountTable] ReadFile '{}': cannot open '{}'.",
                Str(virtualPath).c_str(), physical.c_str());
            return false;
        }

        out.resize(static_cast<size_t>(size));
        if (size == 0)
        {
            return true;
        }

        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (static_cast<uint64_t>(file.gcount()) != static_cast<uint64_t>(size))
        {
            LOG_ERROR("[MountTable] ReadFile '{}': short read ({} of {} bytes).",
                Str(virtualPath).c_str(), static_cast<uint64_t>(file.gcount()),
                static_cast<uint64_t>(size));
            out.clear();
            return false;
        }
        return true;
    }

    bool MountTable::WriteFile(eastl::string_view virtualPath,
                               const uint8_t* data, size_t size) const
    {
        namespace fs = std::filesystem;

        if (size != 0 && !data)
        {
            LOG_ERROR("[MountTable] WriteFile '{}': null data.", Str(virtualPath).c_str());
            return false;
        }

        const eastl::string physical = ToPhysical(virtualPath);
        if (physical.empty())
        {
            return false;
        }

        const fs::path target(ToStd(physical));

        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (ec)
        {
            LOG_ERROR("[MountTable] WriteFile '{}': cannot create '{}': {}",
                Str(virtualPath).c_str(), target.parent_path().generic_string().c_str(),
                ec.message().c_str());
            return false;
        }

        const fs::path temp = MakeTempPath(target);
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                LOG_ERROR("[MountTable] WriteFile '{}': cannot open '{}'.",
                    Str(virtualPath).c_str(), temp.generic_string().c_str());
                return false;
            }
            if (size != 0)
            {
                file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            }
            file.close();
            if (!file)
            {
                LOG_ERROR("[MountTable] WriteFile '{}': write failed.", Str(virtualPath).c_str());
                fs::remove(temp, ec);
                return false;
            }
        }

        fs::rename(temp, target, ec);
        if (!ec)
        {
            return true;
        }

        // Losing the rename to a concurrent writer of the same target is a success: the
        // bytes are a pure function of the path, so whatever landed there is what this
        // call would have written.
        std::error_code existsEc;
        const bool landed = fs::exists(target, existsEc);

        std::error_code removeEc;
        fs::remove(temp, removeEc);

        if (!landed)
        {
            LOG_ERROR("[MountTable] WriteFile '{}': rename failed: {}",
                Str(virtualPath).c_str(), ec.message().c_str());
        }
        return landed;
    }

    bool MountTable::Exists(eastl::string_view virtualPath) const
    {
        namespace fs = std::filesystem;

        const eastl::string physical = ToPhysical(virtualPath);
        if (physical.empty())
        {
            return false;
        }

        std::error_code ec;
        return fs::is_regular_file(fs::path(ToStd(physical)), ec);
    }

    FileStamp MountTable::GetFileStamp(eastl::string_view virtualPath) const
    {
        namespace fs = std::filesystem;

        const eastl::string physical = ToPhysical(virtualPath);
        if (physical.empty())
        {
            return {};
        }

        const fs::path path(ToStd(physical));

        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
        {
            return {};
        }

        const auto written = fs::last_write_time(path, ec);
        if (ec)
        {
            return {};
        }

        const auto size = fs::file_size(path, ec);
        if (ec)
        {
            return {};
        }

        FileStamp stamp;
        // The epoch is implementation-defined, which is fine: the stamp is only ever
        // compared against another stamp taken by this same build.
        stamp.m_modifiedTime = static_cast<uint64_t>(written.time_since_epoch().count());
        stamp.m_size         = static_cast<uint64_t>(size);
        return stamp;
    }
}
