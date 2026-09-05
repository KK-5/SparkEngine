#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>

#include "MaterialAssetCompiler.h"
#include "MaterialAssetLoader.h"

namespace Spark::Resource
{
    //! Material's half of AssetBuildBus.
    //!
    //! `Serialize` is implemented although a material is never cached -- the format is the
    //! type's capability, SaveAsset is its other user -- but it answers only the source-file
    //! half (see the .cpp). `Deserialize` is declined: reading a `.smat` back needs the
    //! AssetId the compiler resolves texture paths against, and Deserialize has none.
    //! Nothing asks until a model's cache unit has to restore its material sub-assets.
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

        bool PrepareToSave(AssetData& data, eastl::string_view virtualPath) override;

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        MaterialAssetLoader   m_loader;
        MaterialAssetCompiler m_compiler;
    };
}
