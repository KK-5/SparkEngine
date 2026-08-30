#include "MaterialAssetBuilder.h"

#include <Log/ILogSystem.h>
#include <Resource/AssetBuildContext.h>

#include "MaterialAsset.h"

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

        const auto& bytes = static_cast<BinaryAssetData&>(*ctx.rawData).GetBytes();

        UniquePtr<AssetData> compiled = m_compiler.Compile(ctx.id, bytes.data(), bytes.size());
        if (!compiled)
        {
            return;
        }

        // Each texture is a file of its own with its own stamp and its own cache key, so
        // it is a dependency rather than a sub-asset -- the same treatment a glTF's
        // external URIs get. ProcessAsset loads them before this material goes Ready.
        const StandardPBR& params = static_cast<MaterialAssetData&>(*compiled).GetParams();
        for (const AssetId& texture : params.m_textures)
        {
            if (!texture.IsValid())
            {
                continue;
            }

            // A sub-asset cannot be loaded on its own -- its bytes live inside its parent's
            // file. Naming one here would otherwise fail deep inside ProcessAsset, pointing
            // at the parent model rather than at the material that asked for it. Extracting
            // the texture into an asset of its own is the way to reference it.
            if (texture.IsSubAsset())
            {
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
