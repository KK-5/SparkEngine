#pragma once

#include <EASTL/atomic.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Object/Object.h>
#include "AssetTypes.h"
#include "Bus/AssetBus.h"


namespace Spark::Resource
{
    class AssetData
    {
    public:
        virtual ~AssetData() = default;

        AssetData(const AssetData&) = delete;
        AssetData& operator=(const AssetData&) = delete;

    protected:
        AssetData() = default;
    };

    class Asset : public Object
    {
    public:
        virtual ~Asset() = default;

        Asset(AssetId id, AssetType type)
            : m_id(eastl::move(id))
            , m_type(type)
        {}

        const AssetId&  GetAssetId() const  { return m_id; }
        AssetType       GetAssetType() const { return m_type; }
        AssetStatus     GetStatus() const   { return m_status.load(eastl::memory_order_acquire); }
        bool            IsReady() const     { return GetStatus() == AssetStatus::Ready; }
        bool            IsError() const     { return GetStatus() == AssetStatus::Error; }
        bool            IsLoading() const   { auto s = GetStatus();
                                              return s == AssetStatus::Loading
                                                  || s == AssetStatus::Queued
                                                  || s == AssetStatus::Compiling; }

        /// 获取资产数据，调用前应确保 IsReady()
        template<typename T>
        T* GetData() const
        {
            return static_cast<T*>(m_data.get());
        }

        void SetStatus(AssetStatus status)  { m_status.store(status, eastl::memory_order_release); }

        void SetDataReady(eastl::unique_ptr<AssetData> data);

    protected:
        void Shutdown() override;

    private:
        AssetId                              m_id;
        AssetType                            m_type;
        eastl::atomic<AssetStatus>           m_status{AssetStatus::NotLoaded};
        eastl::unique_ptr<AssetData>         m_data;
    };

}