#include "AssetLoadBatch.h"

#include <EASTL/sort.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include "AssetManagerInterface.h"


namespace Spark::Resource
{
    namespace
    {
        bool IsSettled(AssetStatus status)
        {
            return status == AssetStatus::Ready || status == AssetStatus::Error;
        }

        AssetLoadProgress::Entry& EntryFor(AssetLoadProgress& progress, AssetType type)
        {
            for (AssetLoadProgress::Entry& entry : progress.entries)
            {
                if (entry.type == type)
                {
                    return entry;
                }
            }

            AssetLoadProgress::Entry fresh;
            fresh.type = type;
            progress.entries.push_back(fresh);
            return progress.entries.back();
        }
    }

    AssetLoadBatch::AssetLoadBatch(eastl::vector<AssetId> ids)
    {
        m_items.reserve(ids.size());
        for (AssetId& id : ids)
        {
            Item item;
            item.id = eastl::move(id);
            m_items.push_back(eastl::move(item));
        }
    }

    void AssetLoadBatch::RequestAll()
    {
        if (m_requested)
        {
            return;
        }
        m_requested = true;

        auto* assetManager = Service<AssetManager>::Get();
        if (!assetManager)
        {
            // Every item keeps its null asset, which counts as failed: the batch completes
            // rather than leaving whoever waits on it without an answer.
            LOG_ERROR("[AssetLoadBatch] No AssetManager; {} assets were not requested.",
                m_items.size());
            return;
        }

        for (Item& item : m_items)
        {
            item.asset = assetManager->RequestAsset(item.id);
            if (!item.asset)
            {
                LOG_ERROR("[AssetLoadBatch] Could not request '{}'.",
                    item.id.GetPath().c_str());
            }
        }
    }

    AssetLoadProgress AssetLoadBatch::GetProgress() const
    {
        AssetLoadProgress progress;
        progress.total    = static_cast<uint32_t>(m_items.size());
        progress.complete = m_requested;

        for (const Item& item : m_items)
        {
            AssetLoadProgress::Entry& entry = EntryFor(progress, item.id.GetAssetType());
            entry.total++;

            if (!m_requested)
            {
                continue;
            }

            // Nothing will arrive to change a null asset, so it settles as an error rather
            // than holding the batch open.
            const AssetStatus status =
                item.asset ? item.asset->GetStatus() : AssetStatus::Error;

            if (status == AssetStatus::Ready)
            {
                entry.ready++;
                progress.ready++;
            }
            else if (status == AssetStatus::Error)
            {
                entry.failed++;
                progress.failed++;
            }
            else
            {
                progress.complete = false;
                if (progress.current.empty()
                    && (status == AssetStatus::Loading || status == AssetStatus::Compiling))
                {
                    progress.current = item.id.GetPath();
                }
            }
        }

        // The ids arrive in the database's hash order; without this a caller could not name
        // a row by position.
        eastl::sort(progress.entries.begin(), progress.entries.end(),
            [](const AssetLoadProgress::Entry& a, const AssetLoadProgress::Entry& b)
        {
            return static_cast<uint32_t>(a.type) < static_cast<uint32_t>(b.type);
        });

        return progress;
    }

    bool AssetLoadBatch::IsComplete() const
    {
        if (!m_requested)
        {
            return false;
        }

        for (const Item& item : m_items)
        {
            if (item.asset && !IsSettled(item.asset->GetStatus()))
            {
                return false;
            }
        }
        return true;
    }
}
