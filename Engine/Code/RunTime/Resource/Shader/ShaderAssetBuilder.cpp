#include "ShaderAssetBuilder.h"

#include <Resource/AssetBuildContext.h>

#include "ShaderAsset.h"


namespace Spark::Resource
{
    HashString ShaderAssetBuilder::GetName() const
    {
        return "ShaderAssetBuilder"_hs;
    }

    void ShaderAssetBuilder::InitInternal()
    {
        m_compiler.AddStageEntry({RHI::ShaderStage::Vertex,   "VSMain", "vs_6_0"});
        m_compiler.AddStageEntry({RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"});

        AssetBuildBus::Handler::BusConnect(AssetType::Shader);
    }

    void ShaderAssetBuilder::ShutdownInternal()
    {
        AssetBuildBus::Handler::BusDisconnect();
    }

    Ptr<Asset> ShaderAssetBuilder::CreateAsset(const AssetId& id)
    {
        return Ptr<Asset>(new ShaderAsset(id));
    }

    void ShaderAssetBuilder::Load(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Shader, "[ShaderAssetBuilder] ctx.type mismatch");
        m_loader.SetSearchPaths(ctx.searchPaths);
        ctx.rawData = m_loader.Load(ctx.id);
    }

    void ShaderAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Shader, "[ShaderAssetBuilder] ctx.type mismatch");
        if (!ctx.rawData)
        {
            return;
        }
        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);
    }
}
