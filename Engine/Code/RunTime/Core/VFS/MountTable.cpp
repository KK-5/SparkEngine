#include "MountTable.h"

#include <filesystem>
#include <string>

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
}
