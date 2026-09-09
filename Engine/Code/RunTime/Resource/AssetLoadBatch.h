#pragma once

#include <cstdint>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "Asset.h"
#include "AssetTypes.h"


namespace Spark::Resource
{
    struct AssetLoadProgress
    {
        struct Entry
        {
            AssetType type{AssetType::Unknown};
            uint32_t  total{0};
            uint32_t  ready{0};
            uint32_t  failed{0};
        };

        eastl::vector<Entry> entries;         ///< one per type present, ordered by AssetType
        uint32_t             total{0};
        uint32_t             ready{0};
        uint32_t             failed{0};
        eastl::string        current;         ///< an asset being built now; empty between two
        bool                 complete{false};
    };

    //! A group of assets loaded together, and the progress of that group. Contents are
    //! fixed at construction: adding to a running batch would send `total` backwards.
    //!
    //! Progress is polled rather than delivered. AssetStatus is an atomic, so there is no
    //! bus subscription and no rule about outliving a dispatch; polling also sees the
    //! assets that were already Ready, which RequestAsset returns without announcing.
    class AssetLoadBatch final
    {
    public:
        explicit AssetLoadBatch(eastl::vector<AssetId> ids);

        AssetLoadBatch(const AssetLoadBatch&)            = delete;
        AssetLoadBatch& operator=(const AssetLoadBatch&) = delete;

        void RequestAll();

        AssetLoadProgress GetProgress() const;

        //! Every asset settled on Ready or Error. False until RequestAll has run.
        bool IsComplete() const;

    private:
        struct Item
        {
            AssetId    id;
            Ptr<Asset> asset;   ///< null when RequestAsset declined it
        };

        eastl::vector<Item> m_items;
        bool                m_requested{false};
    };
}
