#include "ModelAssetBuilder.h"

#include <Resource/AssetBuildContext.h>

#include "ModelAsset.h"


namespace Spark::Resource
{
    HashString ModelAssetBuilder::GetName() const
    {
        return "ModelAssetBuilder"_hs;
    }

    void ModelAssetBuilder::InitInternal()
    {
        AssetBuildBus::Handler::BusConnect(AssetType::Model);
    }

    void ModelAssetBuilder::ShutdownInternal()
    {
        AssetBuildBus::Handler::BusDisconnect();
    }

    Ptr<Asset> ModelAssetBuilder::CreateAsset(const AssetId& id)
    {
        return Ptr<Asset>(new ModelAsset(id));
    }

    void ModelAssetBuilder::Load(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Model, "[ModelAssetBuilder] ctx.type mismatch");
        m_loader.SetSearchPaths(ctx.searchPaths);
        ctx.rawData = m_loader.Load(ctx.id);
    }

    void ModelAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Model, "[ModelAssetBuilder] ctx.type mismatch");
        if (!ctx.rawData)
        {
            return;
        }
        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);
    }
}
