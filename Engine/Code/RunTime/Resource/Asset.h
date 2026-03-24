#pragma once

#include <EASTL/unique_ptr.h>

#include <Object/Object.h>
#include "AssetTypes.h"


namespace Spark::Asset
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
        AssetStatus     GetStatus() const   { return m_status; }
        bool            IsReady() const     { return m_status == AssetStatus::Ready; }
        bool            IsError() const     { return m_status == AssetStatus::Error; }
        bool            IsLoading() const   { return m_status == AssetStatus::Loading
                                                  || m_status == AssetStatus::Queued
                                                  || m_status == AssetStatus::Compiling; }

        /// 获取资产数据，调用前应确保 IsReady()
        template<typename T>
        T* GetData() const
        {
            return static_cast<T*>(m_data.get());
        }

    protected:
        friend class AssetManager;

        void SetStatus(AssetStatus status)  { m_status = status; }
        void SetData(eastl::unique_ptr<AssetData> data);
        void Shutdown() override;

    private:
        AssetId                         m_id;
        AssetType                       m_type;
        AssetStatus                     m_status{AssetStatus::NotLoaded};
        eastl::unique_ptr<AssetData>    m_data;
    };

    struct AssetLoader
    {
        virtual ~AssetLoader() = default;
        virtual eastl::unique_ptr<AssetData> Load(const AssetId& id) = 0;
    };

    struct AssetCompiler
    {
        virtual ~AssetCompiler() = default;
        virtual eastl::unique_ptr<AssetData> Compile(const AssetId& id, AssetData& rawData) = 0;
    };
}