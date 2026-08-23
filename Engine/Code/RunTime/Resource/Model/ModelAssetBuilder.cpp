#include "ModelAssetBuilder.h"

#include <cstdio>
#include <filesystem>

#include <Log/ILogSystem.h>

#include <Resource/AssetBuildContext.h>
#include <Resource/AssetDataBase.h>
#include <Resource/Bus/AssetBuildBus.h>
#include <Resource/Bus/AssetBus.h>
#include <Resource/Image/ImageAsset.h>

#include "ModelAsset.h"


namespace Spark::Resource
{
    namespace
    {
        eastl::string MakeImageSubLabel(const eastl::string& name, size_t index)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "image/%zu", index);
            eastl::string label(buf);
            if (!name.empty())
            {
                label += "/";
                label += name;
            }
            return label;
        }

        void DispatchImageSubAsset(AssetBuildContext& parentCtx,
                                   AssetId subId,
                                   const uint8_t* sourceData,
                                   size_t sourceSize)
        {
            ASSERT(parentCtx.db != nullptr,
                "[ModelAssetBuilder] parent ctx.db not set; cannot dispatch sub-asset");

            // 1. dedup
            if (parentCtx.db->Find(subId))
            {
                return;
            }

            // 2. create + 抢注
            Ptr<Asset> created;
            AssetBuildBus::EventResult(created, AssetType::Image,
                                       &AssetBuildEvents::CreateAsset, subId);
            if (!created)
            {
                LOG_WARN("[ModelAssetBuilder] CreateAsset failed for image '{}'",
                    subId.GetPath().c_str());
                return;
            }
            Ptr<Asset> stored = parentCtx.db->InsertOrGet(subId, created);
            if (stored.get() != created.get())
            {
                return;
            }

            // 3. child ctx
            AssetBuildContext child = parentCtx.MakeChild(subId, AssetType::Image);
            child.sourceData = sourceData;
            child.sourceSize = sourceSize;

            // 4. Load
            stored->SetStatus(AssetStatus::Loading);
            AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Load, child);
            if (!child.rawData)
            {
                stored->SetStatus(AssetStatus::Error);
                AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetError, *stored);
                LOG_WARN("[ModelAssetBuilder] sub-asset Load failed: {}",
                    subId.GetPath().c_str());
                return;
            }

            // 5. Compile
            stored->SetStatus(AssetStatus::Compiling);
            AssetBuildBus::Event(AssetType::Image, &AssetBuildEvents::Compile, child);
            if (!child.compiledData)
            {
                stored->SetStatus(AssetStatus::Error);
                AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetError, *stored);
                LOG_WARN("[ModelAssetBuilder] sub-asset Compile failed: {}",
                    subId.GetPath().c_str());
                return;
            }

            // 6. Ready —— SetDataReady 内部置 status 为 Ready/Error
            stored->SetDataReady(eastl::move(child.compiledData));
            AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetReady, *stored);
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
        ASSERT(ctx.type == AssetType::Model, "[ModelAssetBuilder] ctx.type mismatch");
        ctx.rawData = m_loader.Load(ctx.id, *ctx.fileSystem);
    }

    void ModelAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Model, "[ModelAssetBuilder] ctx.type mismatch");
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

            AssetId subId;
            const uint8_t* src = nullptr;
            size_t srcSize = 0;

            if (!entry.data.empty())
            {
                eastl::string subLabel = MakeImageSubLabel(entry.name, i);
                subId = ImageAsset::MakeSubId(
                    ctx.id, eastl::string_view(subLabel.c_str(), subLabel.size()), usage);
                src     = entry.data.data();
                srcSize = entry.data.size();
            }
            else
            {
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
                subId = AssetId::Of(
                    eastl::string_view(uri.c_str(), uri.size()),
                    ImageAsset::DescriptorForUsage(usage));
            }

            compiled.m_imageAssetIds.push_back(subId);

            DispatchImageSubAsset(ctx, eastl::move(subId), src, srcSize);
        }

        auto resolveImageId = [&](int32_t imageIndex) -> AssetId
        {
            if (imageIndex >= 0 && static_cast<size_t>(imageIndex) < compiled.m_imageAssetIds.size())
            {
                return compiled.m_imageAssetIds[imageIndex];
            }
            return AssetId{};
        };

        compiled.m_materials.reserve(raw.m_rawMaterials.size());
        for (const RawMaterial& rm : raw.m_rawMaterials)
        {
            Material mat;
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
            compiled.m_materials.push_back(eastl::move(mat));
        }

        raw.m_rawImages.clear();
    }
}
