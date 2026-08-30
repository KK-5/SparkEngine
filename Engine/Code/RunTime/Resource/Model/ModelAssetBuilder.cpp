#include "ModelAssetBuilder.h"

#include <cstdio>
#include <filesystem>

#include <Log/ILogSystem.h>

#include <Resource/AssetBuildContext.h>
#include <Resource/AssetDataBase.h>
#include <Resource/Bus/AssetBuildBus.h>
#include <Resource/Bus/AssetBus.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Material/MaterialAsset.h>
#include <Resource/Material/MaterialRawTypes.h>

#include "ModelAsset.h"


namespace Spark::Resource
{
    namespace
    {
        //! Half of every sub-asset's identity: renaming strands every reference to it.
        eastl::string MakeSubLabel(const char* kind, const eastl::string& name, size_t index)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%s/%zu", kind, index);
            eastl::string label(buf);
            if (!name.empty())
            {
                label += "/";
                label += name;
            }
            return label;
        }

        //! A RawMaterial with its image indices resolved to sub-asset ids — the last shape
        //! the glTF material takes before it becomes a material asset of its own.
        struct ResolvedMaterial
        {
            Math::Vector4   baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
            float           metallicFactor{1.0f};
            float           roughnessFactor{1.0f};
            Math::Vector3   emissiveFactor{0.0f, 0.0f, 0.0f};
            float           emissiveStrength{1.0f};   // KHR_materials_emissive_strength
            float           normalScale{1.0f};        // normalTexture.scale
            float           occlusionStrength{1.0f};  // occlusionTexture.strength
            float           alphaCutoff{0.5f};
            AlphaMode       alphaMode{AlphaMode::Opaque};
            bool            doubleSided{false};

            AssetId baseColorImageId;
            AssetId normalImageId;
            AssetId metallicRoughnessImageId;
            AssetId emissiveImageId;
            AssetId occlusionImageId;
        };

        //! Owned on return: AssetData deletes its copy constructor and declares no move.
        UniquePtr<MaterialDecodedRawData> MakeMaterialRaw(const ResolvedMaterial& src)
        {
            StandardPBR params;
            params.m_baseColor         = src.baseColorFactor;
            params.m_metallic          = src.metallicFactor;
            params.m_roughness         = src.roughnessFactor;
            params.m_emissive          = Math::Vector4(src.emissiveFactor, 1.0f);
            params.m_emissiveStrength  = src.emissiveStrength;
            params.m_normalScale       = src.normalScale;
            params.m_occlusionStrength = src.occlusionStrength;

            using Slot = MaterialTexSlot;
            params.m_textures[static_cast<size_t>(Slot::BaseColor)]         = src.baseColorImageId;
            params.m_textures[static_cast<size_t>(Slot::MetallicRoughness)] = src.metallicRoughnessImageId;
            params.m_textures[static_cast<size_t>(Slot::Normal)]            = src.normalImageId;
            params.m_textures[static_cast<size_t>(Slot::Occlusion)]         = src.occlusionImageId;
            params.m_textures[static_cast<size_t>(Slot::Emissive)]          = src.emissiveImageId;

            MaterialState state;
            state.m_alphaMode   = src.alphaMode;
            state.m_alphaCutoff = src.alphaCutoff;
            state.m_doubleSided = src.doubleSided;

            return MakeUnique<MaterialDecodedRawData>(eastl::move(params), state);
        }
    }

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
        ASSERT(ctx.id.GetAssetType() == AssetType::Model, "[ModelAssetBuilder] asset type mismatch");
        ctx.rawData = m_loader.Load(ctx.id, *ctx.fileSystem);
    }

    void ModelAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.id.GetAssetType() == AssetType::Model, "[ModelAssetBuilder] asset type mismatch");
        if (!ctx.rawData)
        {
            return;
        }

        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);

        auto& raw      = static_cast<ModelAssetRawData&>(*ctx.rawData);
        auto& compiled = static_cast<ModelAssetData&>(*ctx.compiledData);

        // Per-image usage, driven by the material slot that references each image. glTF
        // declares the slot (baseColorTexture / normalTexture / ...), so this reads a
        // request — not an inference of the image's "true" nature. DescriptorForUsage then
        // pins colorSpace per image at dispatch (base color / emissive → sRGB; MR /
        // occlusion → linear; normal → linear normal map). Unreferenced images keep the
        // sRGB Texture2D default, matching the historical behaviour (stable AssetIds).
        // One image referenced by slots of differing usage is deliberately out of scope:
        // that's a runtime / node-material concern; here we parse each image as requested.
        eastl::vector<ImageUsage> imageUsages(raw.m_rawImages.size(), ImageUsage::Texture2D);
        auto tagImageUsage = [&](int32_t imageIndex, ImageUsage usage)
        {
            if (imageIndex >= 0 && static_cast<size_t>(imageIndex) < imageUsages.size())
            {
                imageUsages[imageIndex] = usage;
            }
        };
        for (const RawMaterial& rm : raw.m_rawMaterials)
        {
            tagImageUsage(rm.baseColorImage,         ImageUsage::Texture2D);
            tagImageUsage(rm.emissiveImage,          ImageUsage::Texture2D);
            tagImageUsage(rm.metallicRoughnessImage, ImageUsage::NoColorTexture2D);
            tagImageUsage(rm.occlusionImage,         ImageUsage::NoColorTexture2D);
            tagImageUsage(rm.normalImage,            ImageUsage::NormalMap);
        }

        compiled.m_imageAssetIds.reserve(raw.m_rawImages.size());
        for (size_t i = 0; i < raw.m_rawImages.size(); ++i)
        {
            auto& entry = raw.m_rawImages[i];
            const ImageUsage usage = imageUsages[i];

            if (entry.data.empty() && entry.externalUri.empty())
            {
                compiled.m_imageAssetIds.push_back(AssetId{});
                continue;
            }

            if (!entry.data.empty())
            {
                // Embedded: the bytes are inside this file, so the image is a sub-asset of
                // it. `sourceData` points into raw.m_rawImages, which stays alive for as
                // long as ctx.rawData does -- past the publish that consumes it.
                eastl::string subLabel = MakeSubLabel("image", entry.name, i);
                AssetId subId = ImageAsset::MakeSubId(
                    ctx.id, eastl::string_view(subLabel.c_str(), subLabel.size()), usage);

                compiled.m_imageAssetIds.push_back(subId);
                ctx.subAssets.push_back(
                    {eastl::move(subId), nullptr, entry.data.data(), entry.data.size()});
                continue;
            }

            // External: its own file, its own stamp, its own cache key. A dependency, not a
            // sub-asset -- it is loaded as an ordinary asset in its own right.
            //
            // The URI is relative to the glTF file, so it resolves against the parent's
            // virtual directory. Purely lexical -- no directory joins the search.
            const eastl::string uri = ResolveSiblingVirtualPath(
                ctx.id.GetPath(),
                eastl::string_view(entry.externalUri.c_str(), entry.externalUri.size()));
            if (uri.empty())
            {
                compiled.m_imageAssetIds.push_back(AssetId{});
                continue;
            }

            AssetId depId = AssetId::Of(
                eastl::string_view(uri.c_str(), uri.size()), {}, AssetType::Image,
                ImageAsset::DescriptorForUsage(usage));

            compiled.m_imageAssetIds.push_back(depId);
            ctx.dependencies.push_back(eastl::move(depId));
        }

        auto resolveImageId = [&](int32_t imageIndex) -> AssetId
        {
            if (imageIndex >= 0 && static_cast<size_t>(imageIndex) < compiled.m_imageAssetIds.size())
            {
                return compiled.m_imageAssetIds[imageIndex];
            }
            return AssetId{};
        };

        // Handed over decoded, not as bytes: in a glTF a material is structure.
        compiled.m_materialAssetIds.reserve(raw.m_rawMaterials.size());
        for (size_t i = 0; i < raw.m_rawMaterials.size(); ++i)
        {
            const RawMaterial& rm = raw.m_rawMaterials[i];

            ResolvedMaterial mat;
            mat.doubleSided       = rm.doubleSided;
            mat.baseColorFactor   = rm.baseColorFactor;
            mat.metallicFactor    = rm.metallicFactor;
            mat.roughnessFactor   = rm.roughnessFactor;
            mat.emissiveFactor    = rm.emissiveFactor;
            mat.emissiveStrength  = rm.emissiveStrength;
            mat.normalScale       = rm.normalScale;
            mat.occlusionStrength = rm.occlusionStrength;
            mat.alphaCutoff       = rm.alphaCutoff;
            mat.alphaMode         = rm.alphaMode;
            mat.baseColorImageId         = resolveImageId(rm.baseColorImage);
            mat.metallicRoughnessImageId = resolveImageId(rm.metallicRoughnessImage);
            mat.normalImageId            = resolveImageId(rm.normalImage);
            mat.occlusionImageId         = resolveImageId(rm.occlusionImage);
            mat.emissiveImageId          = resolveImageId(rm.emissiveImage);

            const eastl::string subLabel = MakeSubLabel("material", rm.name, i);
            AssetId subId = MaterialAsset::MakeSubId(
                ctx.id, eastl::string_view(subLabel.c_str(), subLabel.size()));

            compiled.m_materialAssetIds.push_back(subId);
            ctx.subAssets.push_back(
                {eastl::move(subId), MakeMaterialRaw(mat), nullptr, 0});
        }

        // m_rawImages is deliberately NOT cleared: the sub-asset declarations above point
        // into it, and they are read after this returns.
    }
}
