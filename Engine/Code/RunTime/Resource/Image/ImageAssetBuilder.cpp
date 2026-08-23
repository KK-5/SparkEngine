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
        ASSERT(ctx.type == AssetType::Image, "[ImageAssetBuilder] ctx.type mismatch");

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

        // Which slot the result lands in IS the signal: a payload that arrives already
        // compiled goes straight to compiledData, and ProcessAsset then skips Compile.
        bool isCompiled = false;
        UniquePtr<AssetData> loaded = m_loader.Load(ctx.id, *ctx.fileSystem, isCompiled);
        if (isCompiled)
        {
            ctx.compiledData = eastl::move(loaded);
        }
        else
        {
            ctx.rawData = eastl::move(loaded);
        }
    }

    bool ImageAssetBuilder::InitEnvironmentBaker()
    {
        return m_baker.Init();
    }

    void ImageAssetBuilder::Compile(AssetBuildContext& ctx)
    {
        ASSERT(ctx.type == AssetType::Image, "[ImageAssetBuilder] ctx.type mismatch");

        const ImageAssetDescriptor* desc = GetImageDescriptor(ctx.id);
        if (IsDerivedUsage(desc))
        {
            LOG_ERROR("[ImageAssetBuilder] '{}' is a derived IBL product and cannot be "
                      "compiled on its own; it is published by its parent environment "
                      "cubemap's bake.", ctx.id.GetPath().c_str());
            return;
        }

        if (!ctx.rawData)
        {
            return;
        }

        // EnvironmentCubemap usage routes through the GPU baker: the equirect raw is
        // baked into a 6-face cube and wrapped as a cube ImageAssetData. The default
        // Texture2D path (mip-gen + optional BCn) is untouched.
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
        if (!m_baker.IsInitialized())
        {
            LOG_ERROR("[ImageAssetBuilder] EnvironmentCubemap asset requested but the "
                      "baker is not initialized (call InitEnvironmentBaker during setup): {}",
                      ctx.id.GetPath().c_str());
            return nullptr;
        }

        auto& raw = static_cast<ImageAssetRawData&>(*ctx.rawData);
        // cubemapFaceSize == 0 means auto: size the cube from the decoded source.
        const uint32_t faceSize = desc.cubemapFaceSize != 0
            ? desc.cubemapFaceSize
            : EnvironmentBaker::RecommendedFaceSize(raw.GetHeight());

        BakedEnvironment env = m_baker.Bake(raw, faceSize);
        // All or nothing: a partial result would leave the lighting path unable to tell
        // "no IBL here" from "IBL half-baked".
        if (!env.IsValid())
        {
            LOG_ERROR("[ImageAssetBuilder] EnvironmentBaker failed for {}",
                      ctx.id.GetPath().c_str());
            return nullptr;
        }

        // Publish before returning: AssetManager marks this asset Ready only after Compile
        // returns, so anyone seeing the sky cube go Ready finds both children already Ready.
        Ptr<Asset> irradiance = PublishSubAsset(
            ctx,
            ImageAsset::MakeSubId(ctx.id, kIrradianceSubLabel, ImageUsage::IrradianceCubemap),
            m_compiler.AssembleCubemapData(eastl::move(env.irradiance)));

        Ptr<Asset> prefiltered = PublishSubAsset(
            ctx,
            ImageAsset::MakeSubId(ctx.id, kPrefilteredSubLabel, ImageUsage::PrefilteredCubemap),
            m_compiler.AssembleCubemapData(eastl::move(env.prefiltered)));

        if (!irradiance || !prefiltered)
        {
            LOG_ERROR("[ImageAssetBuilder] Failed to publish the IBL sub-assets of {}",
                      ctx.id.GetPath().c_str());
            return nullptr;
        }

        UniquePtr<AssetData> skyData = m_compiler.AssembleCubemapData(eastl::move(env.sky));
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
                                                  UniquePtr<AssetData> compiled)
    {
        ASSERT(parentCtx.db != nullptr,
            "[ImageAssetBuilder] parent ctx.db not set; cannot publish sub-asset");
        if (!parentCtx.db || !compiled)
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
