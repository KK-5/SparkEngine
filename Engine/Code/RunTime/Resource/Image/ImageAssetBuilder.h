#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>

#include "ImageAssetLoader.h"
#include "ImageAssetCompiler.h"


namespace Spark::Resource
{
    //! Image's half of AssetBuildBus, and a long-lived System so it can hold the loader and
    //! compiler that do the actual work. Its own job is asset lifecycle: which loader a path
    //! calls for, which usage compiles how, and what sub-assets a build declares.
    class ImageAssetBuilder final : public ISystem,
                                    public AssetBuildBus::Handler
    {
    public:
        ImageAssetBuilder() = default;
        ~ImageAssetBuilder() override = default;

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

        bool InitEnvironmentBaker();

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        ImageAssetLoader   m_loader;
        ImageAssetCompiler m_compiler;
    };
}
