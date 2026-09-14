#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>

#include "ModelAssetLoader.h"
#include "ModelAssetCompiler.h"


namespace Spark::Resource
{
    class ModelAssetBuilder final : public ISystem,
                                    public AssetBuildBus::Handler
    {
    public:
        ModelAssetBuilder() = default;
        ~ModelAssetBuilder() override = default;

        // ISystem
        eastl::vector<HashString> Request() const override { return {}; }
        HashString                GetName() const override;

        // AssetBuildBus::Handler
        Ptr<Asset> CreateAsset(const AssetId& id) override;
        void       Load(AssetBuildContext& ctx) override;
        void       Compile(AssetBuildContext& ctx) override;

        eastl::vector<uint8_t> Serialize(const AssetData& compiled,
                                         eastl::string_view identity) override;
        UniquePtr<AssetData>   Deserialize(const uint8_t* bytes, size_t size,
                                           eastl::string_view identity) override;

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        ModelAssetLoader   m_loader;
        ModelAssetCompiler m_compiler;
    };
}
