#pragma once

#include <ECS/ISystem.h>
#include <Resource/EBus/AssetBuildBus.h>

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

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        ModelAssetLoader   m_loader;
        ModelAssetCompiler m_compiler;
    };
}
