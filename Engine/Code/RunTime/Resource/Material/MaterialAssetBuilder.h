#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>

#include "MaterialAssetCompiler.h"
#include "MaterialAssetLoader.h"

namespace Spark::Resource
{
    //! Material's half of AssetBuildBus. A `.smat` root is never cached; the format's cache
    //! half exists for the material sub-assets inside a model's unit.
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

        eastl::vector<uint8_t> Serialize(const AssetData& compiled,
                                         eastl::string_view identity) override;
        UniquePtr<AssetData>   Deserialize(const uint8_t* bytes, size_t size,
                                           eastl::string_view identity) override;

        bool PrepareToSave(AssetData& data, eastl::string_view virtualPath) override;

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        MaterialAssetLoader   m_loader;
        MaterialAssetCompiler m_compiler;
    };
}
