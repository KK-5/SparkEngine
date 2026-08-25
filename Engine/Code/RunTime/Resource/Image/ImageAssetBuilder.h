#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>

#include "ImageAssetLoader.h"
#include "ImageAssetCompiler.h"
#include "EnvironmentBaker.h"


namespace Spark::Resource
{
    /// Image 资产的构建器，本身就是一个长生命周期 System：
    /// - Init: 创建内部 Loader/Compiler 并 BusConnect 到 AssetType::Image
    /// - Shutdown: BusDisconnect
    /// 委托现有的 ImageAssetLoader / ImageAssetCompiler 完成实际 I/O 与编译。
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

        //! Register an already-compiled sub-asset, skipping Load/Compile: the IBL products
        //! have no source bytes, their data comes from the parent's bake. Always overwrites
        //! an existing entry -- a re-processed parent means the HDRI changed.
        //! Returns the db-owned asset, or null on failure.
        Ptr<Asset> PublishSubAsset(AssetBuildContext& parentCtx, const AssetId& subId,
                                   UniquePtr<AssetData> compiled);

        //! Bakes the equirect raw, publishes irradiance / prefiltered as sub-assets, and
        //! returns the sky cube's data holding refs to them.
        UniquePtr<AssetData> CompileEnvironmentCubemap(AssetBuildContext& ctx,
                                                       const ImageAssetDescriptor& desc);

        ImageAssetLoader   m_loader;
        ImageAssetCompiler m_compiler;
        EnvironmentBaker   m_baker;
    };
}
