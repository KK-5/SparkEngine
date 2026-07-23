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
            if (!name.empty())
            {
                return eastl::string("image/") + name;
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "image/%zu", index);
            return eastl::string(buf);
        }

        /// 派发一张图片作为子资产。Builder 不依赖 SparkAssetManager —— 编排序列
        /// 通过 AssetBuildBus / AssetBus / ctx.db 横向完成，与 SparkAssetManager::ProcessAsset
        /// 的协议保持一致（协议固化在 Bus 接口本身）。
        void DispatchImageSubAsset(AssetBuildContext& parentCtx,
                                   AssetId subId,
                                   const uint8_t* sourceData,
                                   size_t sourceSize,
                                   eastl::vector<eastl::string> extraSearchPaths)
        {
            ASSERT(parentCtx.db != nullptr,
                "[ModelAssetBuilder] parent ctx.db not set; cannot dispatch sub-asset");

            // 1. dedup —— 外部图被多个模型 / 同图被复用时直接命中
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
            for (auto& p : extraSearchPaths)
            {
                child.searchPaths.insert(child.searchPaths.begin(), eastl::move(p));
            }

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

        // 1. 几何编译
        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);

        // 2. 图片子资产派发 —— 用 raw 端的 m_rawImages（compiledData 上恒为空），
        //    同时把对应 AssetId 写到 compiledData.m_imageAssetIds，index 对齐
        //    raw.m_rawImages / glTF images[]；无效槽位 push 默认 AssetId 占位。
        auto& raw      = static_cast<ModelAssetRawData&>(*ctx.rawData);
        auto& compiled = static_cast<ModelAssetData&>(*ctx.compiledData);

        // glTF 父目录：外部图相对路径的解析基准
        eastl::string modelDir;
        {
            auto parent = std::filesystem::path(raw.GetResolvedPath().c_str())
                .parent_path().string();
            modelDir.assign(parent.c_str(), parent.size());
        }

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

            // 空 entry（Loader 标记的不支持源）—— 占位保对齐，不派发
            if (entry.data.empty() && entry.externalUri.empty())
            {
                compiled.m_imageAssetIds.push_back(AssetId{});
                continue;
            }

            AssetId subId;
            const uint8_t* src = nullptr;
            size_t srcSize = 0;
            eastl::vector<eastl::string> extra;

            if (!entry.data.empty())
            {
                eastl::string subLabel = MakeImageSubLabel(entry.name, i);
                subId = AssetId::OfSub<ImageAsset>(
                    eastl::string_view(ctx.id.GetPath().c_str(), ctx.id.GetPath().size()),
                    eastl::string_view(subLabel.c_str(), subLabel.size()),
                    static_cast<const ImageAssetDescriptor&>(*ImageAsset::DescriptorForUsage(usage)));
                src     = entry.data.data();
                srcSize = entry.data.size();
            }
            else
            {
                subId = AssetId::Of(
                    eastl::string_view(entry.externalUri.c_str(), entry.externalUri.size()),
                    ImageAsset::DescriptorForUsage(usage));
                if (!modelDir.empty())
                {
                    extra.push_back(modelDir);
                }
            }

            // 先记录 AssetId（包含 dispatch 失败 / dedup 命中的情形：runtime 仍
            // 能通过 Find 拿到现有或 Error 状态的 ImageAsset）
            compiled.m_imageAssetIds.push_back(subId);

            DispatchImageSubAsset(ctx, eastl::move(subId), src, srcSize, eastl::move(extra));
        }

        // 3. 组装材质 —— 从 raw.m_rawMaterials（factors + glTF image 索引）产出 compiled.m_materials
        //    （factors + 已解析的 image 子资产 AssetId）。此刻 m_imageAssetIds 就绪。
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

        // 4. 主动释放内嵌图字节（raw 随构建结束析构，这里提早释放）
        raw.m_rawImages.clear();
    }
}
