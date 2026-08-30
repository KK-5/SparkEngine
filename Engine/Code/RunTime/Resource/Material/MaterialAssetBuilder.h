#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>

#include "MaterialAssetCompiler.h"
#include "MaterialAssetLoader.h"

namespace Spark::Resource
{
    //! Material's half of AssetBuildBus. Serialize / Deserialize are deliberately not
    //! overridden: a material has no cache format, and the base class already declines.
    class MaterialAssetBuilder final : public ISystem,
                                       public AssetBuildBus::Handler
    {
    public:
        MaterialAssetBuilder() = default;
        ~MaterialAssetBuilder() override = default;

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

        MaterialAssetLoader   m_loader;
        MaterialAssetCompiler m_compiler;
    };
}
