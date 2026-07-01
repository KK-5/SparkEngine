#include "ImageAssetBuilder.h"

#include <EASTL/utility.h>

#include <Log/ILogSystem.h>
#include <Resource/AssetBuildContext.h>

#include "ImageAsset.h"


namespace Spark::Resource
{
    HashString ImageAssetBuilder::GetName() const
    {
        return "ImageAssetBuilder"_hs;
    }

    void ImageAssetBuilder::InitInternal()
    {
        AssetBuildBus::Handler::BusConnect(AssetType::Image);
    }

    void ImageAssetBuilder::ShutdownInternal()
    {
        AssetBuildBus::Handler::BusDisconnect();
    }

    Ptr<Asset> ImageAssetBuilder::CreateAsset(const AssetId& id)
    {
        return Ptr<Asset>(new ImageAsset(id));
    }

    void ImageAssetBuilder::Load(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Image, "[ImageAssetBuilder] ctx.type mismatch");

        if (ctx.sourceData)
        {
            ctx.rawData = ImageAssetLoader::DecodeFromMemory(
                ctx.sourceData, ctx.sourceSize, ctx.id.GetSubLabel());
            return;
        }
        m_loader.SetSearchPaths(ctx.searchPaths);
        ctx.rawData = m_loader.Load(ctx.id);
    }

    bool ImageAssetBuilder::InitEnvironmentBaker()
    {
        return m_baker.Init();
    }

    void ImageAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Image, "[ImageAssetBuilder] ctx.type mismatch");
        if (!ctx.rawData)
        {
            return;
        }

        // EnvironmentCubemap usage routes through the GPU baker: the equirect raw is
        // baked into a 6-face cube and wrapped as a cube ImageAssetData. The default
        // Texture2D path (mip-gen + optional BCn) is untouched.
        const auto* desc = static_cast<const ImageAssetDescriptor*>(ctx.id.GetDescriptor());
        if (desc && desc->usage == ImageUsage::EnvironmentCubemap)
        {
            if (!m_baker.IsInitialized())
            {
                LOG_ERROR("[ImageAssetBuilder] EnvironmentCubemap asset requested but the "
                          "baker is not initialized (call InitEnvironmentBaker during setup): {}",
                          ctx.id.GetPath().c_str());
                return;
            }

            auto& raw = static_cast<ImageAssetRawData&>(*ctx.rawData);
            // cubemapFaceSize == 0 means auto: size the cube from the decoded source.
            const uint32_t faceSize = desc->cubemapFaceSize != 0
                ? desc->cubemapFaceSize
                : EnvironmentBaker::RecommendedFaceSize(raw.GetHeight());
            BakedCubemap baked = m_baker.Bake(raw, faceSize);
            if (!baked.IsValid())
            {
                LOG_ERROR("[ImageAssetBuilder] EnvironmentBaker failed for {}",
                          ctx.id.GetPath().c_str());
                return;
            }
            ctx.compiledData = m_compiler.AssembleCubemapData(eastl::move(baked));
            return;
        }

        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);
    }
}
