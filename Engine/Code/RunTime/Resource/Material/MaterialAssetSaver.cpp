#include "MaterialAssetSaver.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <Resource/AssetManagerInterface.h>

#include "MaterialAsset.h"
#include "MaterialAssetWriter.h"

namespace Spark::Resource
{
    namespace
    {
        //! Static because nothing outside a save asks: the editor reports at the moment of
        //! the action rather than pre-checking to grey a button out.
        const AssetId* FindEmbeddedTexture(const StandardPBR& params, size_t& outSlot)
        {
            for (size_t slot = 0; slot < params.m_textures.size(); ++slot)
            {
                const AssetId& id = params.m_textures[slot];
                if (id.IsValid() && id.IsSubAsset())
                {
                    outSlot = slot;
                    return &id;
                }
            }
            return nullptr;
        }
    }

    MaterialSaveResult SaveMaterialAsset(const MaterialAssetData& data,
                                         eastl::string_view virtualPath, AssetId& out)
    {
        size_t         slot     = 0;
        const AssetId* embedded = FindEmbeddedTexture(data.GetParams(), slot);
        if (embedded)
        {
            LOG_ERROR("[MaterialAssetSaver] Texture slot {} holds '{}:{}', which lives inside "
                      "a model file and cannot be loaded on its own. Writing '{}' would "
                      "produce a material that loses its textures on the next run.",
                      slot, embedded->GetPath(), embedded->GetSubLabel(), virtualPath);
            return MaterialSaveResult::EmbeddedTexture;
        }

        const eastl::vector<uint8_t> bytes = WriteMaterialAsset(data);
        if (bytes.empty())
        {
            return MaterialSaveResult::Failed;
        }

        auto* assetManager = Service<AssetManager>::Get();
        if (!assetManager)
        {
            LOG_ERROR("[MaterialAssetSaver] No AssetManager registered.");
            return MaterialSaveResult::Failed;
        }

        const AssetId id = assetManager->WriteAssetFile(virtualPath, bytes.data(), bytes.size());
        if (!id.IsValid())
        {
            return MaterialSaveResult::Failed;
        }

        out = id;
        return MaterialSaveResult::Ok;
    }
}
