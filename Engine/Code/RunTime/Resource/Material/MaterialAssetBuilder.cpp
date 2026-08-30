#include "MaterialAssetBuilder.h"

#include <Log/ILogSystem.h>
#include <Resource/AssetBuildContext.h>

#include "MaterialAsset.h"
#include "MaterialRawTypes.h"

namespace Spark::Resource
{
    HashString MaterialAssetBuilder::GetName() const
    {
        return "MaterialAssetBuilder"_hs;
    }

    void MaterialAssetBuilder::InitInternal()
    {
        AssetBuildBus::Handler::BusConnect(AssetType::Material);
    }

    void MaterialAssetBuilder::ShutdownInternal()
    {
        AssetBuildBus::Handler::BusDisconnect();
    }

    Ptr<Asset> MaterialAssetBuilder::CreateAsset(const AssetId& id)
    {
        return Ptr<Asset>(new MaterialAsset(id));
    }

    void MaterialAssetBuilder::Load(AssetBuildContext& ctx)
    {
        ASSERT(ctx.id.GetAssetType() == AssetType::Material, "[MaterialAssetBuilder] asset type mismatch");
        ctx.rawData = m_loader.Load(ctx.id, *ctx.fileSystem);
    }

    void MaterialAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.id.GetAssetType() == AssetType::Material, "[MaterialAssetBuilder] asset type mismatch");
        if (!ctx.rawData)
        {
            return;
        }

        UniquePtr<AssetData> compiled =
            m_compiler.Compile(ctx.id, static_cast<MaterialRawData&>(*ctx.rawData));
        if (!compiled)
        {
            return;
        }

        // A texture in a file of its own has its own stamp and cache key: a dependency.
        const StandardPBR& params = static_cast<MaterialAssetData&>(*compiled).GetParams();
        for (const AssetId& texture : params.m_textures)
        {
            if (!texture.IsValid())
            {
                continue;
            }

            if (texture.IsSubAsset())
            {
                // Same root: already a member of this unit.
                if (texture.GetPath() == ctx.id.GetPath())
                {
                    continue;
                }

                LOG_ERROR("[MaterialAssetBuilder] '{}' references sub-asset '{}:{}'. A "
                          "sub-asset cannot be loaded on its own; extract it into an asset "
                          "of its own first.",
                    ctx.id.GetPath().c_str(), texture.GetPath().c_str(),
                    texture.GetSubLabel().c_str());
                return;
            }

            ctx.dependencies.push_back(texture);
        }

        ctx.compiledData = eastl::move(compiled);
    }
}
