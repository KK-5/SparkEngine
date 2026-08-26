#include "ImageAssetBuilder.h"

#include <EASTL/utility.h>

#include <Log/ILogSystem.h>
#include <Resource/AssetBuildContext.h>
#include <Resource/AssetDataBase.h>
#include <Resource/Bus/AssetBus.h>

#include "ImageAsset.h"


namespace Spark::Resource
{
    namespace
    {
        constexpr const char* kIrradianceSubLabel  = "ibl/irradiance";
        constexpr const char* kPrefilteredSubLabel = "ibl/prefiltered";

        const ImageAssetDescriptor* GetImageDescriptor(const AssetId& id)
        {
            return static_cast<const ImageAssetDescriptor*>(id.GetDescriptor());
        }

        //! The IBL products must never be built on their own: their AssetId path points at
        //! the parent HDRI, so an independent build would *succeed* at loading that file and
        //! compile it down the generic 2D path, silently replacing the baked cube.
        bool IsDerivedUsage(const ImageAssetDescriptor* desc)
        {
            return desc && (desc->usage == ImageUsage::IrradianceCubemap
                         || desc->usage == ImageUsage::PrefilteredCubemap);
        }
    }

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
        ASSERT(ctx.id.GetAssetType() == AssetType::Image, "[ImageAssetBuilder] asset type mismatch");

        if (IsDerivedUsage(GetImageDescriptor(ctx.id)))
        {
            LOG_ERROR("[ImageAssetBuilder] '{}' is a derived IBL product and cannot be "
                      "loaded on its own; it is published by its parent environment "
                      "cubemap's bake.", ctx.id.GetPath().c_str());
            return;
        }

        if (ctx.sourceData)
        {
            ctx.rawData = ImageAssetLoader::DecodeFromMemory(
                ctx.sourceData, ctx.sourceSize, ctx.id.GetSubLabel());
            return;
        }

        // Both land in rawData: an authored .ktx2 is a source format like any other, and
        // parsing it is its Compile.
        ctx.rawData = IsCompiledImagePath(ctx.id.GetPath())
            ? m_loader.LoadEncoded(ctx.id, *ctx.fileSystem)
            : m_loader.LoadSource(ctx.id, *ctx.fileSystem);
    }

    eastl::vector<uint8_t> ImageAssetBuilder::Serialize(const AssetData& compiled,
                                                        eastl::string_view identity)
    {
        const auto& image = static_cast<const ImageAssetData&>(compiled);

        if (image.GetIrradianceAsset() || image.GetPrefilteredAsset())
        {
            return {};
        }
        return m_compiler.SerializeToKtx2(image, identity);
    }

    UniquePtr<AssetData> ImageAssetBuilder::Deserialize(const uint8_t* bytes, size_t size,
                                                        eastl::string_view identity)
    {
        return m_loader.LoadKtx2(bytes, size, "cache entry", identity);
    }

    bool ImageAssetBuilder::InitEnvironmentBaker()
    {
        return m_compiler.InitEnvironmentBaker();
    }

    void ImageAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.id.GetAssetType() == AssetType::Image, "[ImageAssetBuilder] asset type mismatch");

        if (!ctx.rawData)
        {
            return;
        }

        const ImageAssetDescriptor* desc = GetImageDescriptor(ctx.id);
        const auto& raw = static_cast<const ImageRawData&>(*ctx.rawData);

        // Any other raw means one was requested on its own; its AssetId points at the parent
        // HDRI, so this would compile that file down the 2D path over the baked cube.
        if (IsDerivedUsage(desc) && raw.GetKind() != ImageRawData::Kind::Baked)
        {
            LOG_ERROR("[ImageAssetBuilder] '{}' is a derived IBL product and cannot be "
                      "compiled on its own; it is published by its parent environment "
                      "cubemap's bake.", ctx.id.GetPath().c_str());
            return;
        }

        // A bake makes three assets, and registering assets is this class's job -- which is
        // the only reason it is spelled out here instead of inside the compiler's Compile.
        if (desc && desc->usage == ImageUsage::EnvironmentCubemap)
        {
            ctx.compiledData = CompileEnvironmentCubemap(ctx, *desc);
            return;
        }

        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData);
    }

    UniquePtr<AssetData> ImageAssetBuilder::CompileEnvironmentCubemap(
        AssetBuildContext& ctx, const ImageAssetDescriptor& desc)
    {
        BakedEnvironment env = m_compiler.BakeEnvironment(
            ctx.id, static_cast<const ImageAssetRawData&>(*ctx.rawData), desc);

        // All or nothing: a partial result would leave the lighting path unable to tell
        // "no IBL here" from "IBL half-baked".
        if (!env.IsValid())
        {
            LOG_ERROR("[ImageAssetBuilder] Environment bake failed for {}",
                      ctx.id.GetPath().c_str());
            return nullptr;
        }

        // Publish before returning: AssetManager marks this asset Ready only after Compile
        // returns, so anyone seeing the sky cube go Ready finds both children already Ready.
        Ptr<Asset> irradiance = PublishSubAsset(
            ctx,
            ImageAsset::MakeSubId(ctx.id, kIrradianceSubLabel, ImageUsage::IrradianceCubemap),
            MakeUnique<ImageBakedRawData>(eastl::move(env.irradiance)));

        Ptr<Asset> prefiltered = PublishSubAsset(
            ctx,
            ImageAsset::MakeSubId(ctx.id, kPrefilteredSubLabel, ImageUsage::PrefilteredCubemap),
            MakeUnique<ImageBakedRawData>(eastl::move(env.prefiltered)));

        if (!irradiance || !prefiltered)
        {
            LOG_ERROR("[ImageAssetBuilder] Failed to publish the IBL sub-assets of {}",
                      ctx.id.GetPath().c_str());
            return nullptr;
        }

        // The sky goes through the same Compile as its two children.
        ImageBakedRawData skyRaw(eastl::move(env.sky));
        UniquePtr<AssetData> skyData = m_compiler.Compile(ctx.id, skyRaw);
        if (!skyData)
        {
            return nullptr;
        }

        auto& sky = static_cast<ImageAssetData&>(*skyData);
        sky.m_irradiance  = Ptr<ImageAsset>(static_cast<ImageAsset*>(irradiance.get()));
        sky.m_prefiltered = Ptr<ImageAsset>(static_cast<ImageAsset*>(prefiltered.get()));

        LOG_INFO("[ImageAssetBuilder] Environment bake {}: sky {}^2 x{} mips, "
                 "irradiance {}^2 x{}, prefiltered {}^2 x{} (6 faces each)",
                 ctx.id.GetPath().c_str(),
                 sky.GetWidth(), sky.GetMipLevels(),
                 sky.GetIrradianceAsset()->GetWidth(),
                 sky.GetIrradianceAsset()->GetMipLevels(),
                 sky.GetPrefilteredAsset()->GetWidth(),
                 sky.GetPrefilteredAsset()->GetMipLevels());

        return skyData;
    }

    Ptr<Asset> ImageAssetBuilder::PublishSubAsset(AssetBuildContext& parentCtx,
                                                  const AssetId& subId,
                                                  UniquePtr<AssetData> rawData)
    {
        ASSERT(parentCtx.db != nullptr,
            "[ImageAssetBuilder] parent ctx.db not set; cannot publish sub-asset");
        if (!parentCtx.db || !rawData)
        {
            return nullptr;
        }

        // The ordinary Compile: what makes this a derived product is only the raw it gets.
        AssetBuildContext child = parentCtx.MakeChild(subId);
        child.rawData = eastl::move(rawData);
        Compile(child);

        UniquePtr<AssetData> compiled = eastl::move(child.compiledData);
        if (!compiled)
        {
            return nullptr;
        }

        Ptr<Asset> created = CreateAsset(subId);
        if (!created)
        {
            LOG_WARN("[ImageAssetBuilder] CreateAsset failed for sub-asset '{}'",
                     subId.GetSubLabel().c_str());
            return nullptr;
        }

        // On a re-process this returns the existing instance -- the one everyone already
        // holds -- so that is the one to hand the fresh data to.
        Ptr<Asset> stored = parentCtx.db->InsertOrGet(subId, created);
        stored->SetDataReady(eastl::move(compiled));
        AssetBus::Event(AssetType::Image, &AssetBus::Events::OnAssetReady, *stored);
        return stored;
    }
}
