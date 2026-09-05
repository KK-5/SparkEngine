#include "MaterialAssetBuilder.h"

#include <Log/ILogSystem.h>
#include <Resource/AssetBuildContext.h>

#include "MaterialAsset.h"
#include "MaterialAssetWriter.h"
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

    eastl::vector<uint8_t> MaterialAssetBuilder::Serialize(const AssetData& compiled,
                                                           eastl::string_view identity)
    {
        // An identity means the cache is asking -- a material sub-asset in some model's
        // unit. Without Deserialize that payload could never be read back, and a unit that
        // cannot be restored is worse than no unit.
        if (!identity.empty())
        {
            return {};
        }

        return WriteMaterialAsset(static_cast<const MaterialAssetData&>(compiled));
    }

    bool MaterialAssetBuilder::PrepareToSave(AssetData& data, eastl::string_view virtualPath)
    {
        const StandardPBR& params = static_cast<MaterialAssetData&>(data).GetParams();

        for (size_t slot = 0; slot < params.m_textures.size(); ++slot)
        {
            const AssetId& texture = params.m_textures[slot];
            if (!texture.IsValid() || !texture.IsSubAsset())
            {
                continue;
            }

            // The file would work this session and lose its textures on the next, when
            // ProcessAsset refuses to build a sub-asset alone.
            LOG_ERROR("[MaterialAssetBuilder] Texture slot {} holds '{}:{}', which lives "
                      "inside a model file and cannot be loaded on its own. Writing '{}' "
                      "would produce a material that loses its textures on the next run.",
                slot, texture.GetPath().c_str(), texture.GetSubLabel().c_str(),
                eastl::string(virtualPath.data(), virtualPath.size()).c_str());
            return false;
        }

        return true;
    }
}
