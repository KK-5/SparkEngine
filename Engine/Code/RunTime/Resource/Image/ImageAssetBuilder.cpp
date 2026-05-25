#include "ImageAssetBuilder.h"

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

    void ImageAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Image, "[ImageAssetBuilder] ctx.type mismatch");
        if (!ctx.rawData)
        {
            return;
        }
        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);
    }
}
