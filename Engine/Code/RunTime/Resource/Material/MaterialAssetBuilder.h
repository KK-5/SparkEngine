#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>
#include <Resource/Common/CommonAssetLoader.h>

#include "MaterialAssetCompiler.h"

namespace Spark::Resource
{
    //! Material's half of AssetBuildBus. Load is a plain byte read -- a `.smat` is text,
    //! so the shared BinaryAssetLoader is the whole of it -- and Compile is the parse.
    //!
    //! Serialize / Deserialize are deliberately not overridden: a material has no cache
    //! format (GetCacheFormat says so), and the base class already declines.
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

        BinaryAssetLoader     m_loader;
        MaterialAssetCompiler m_compiler;
    };
}
