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
        return m_compiler.SerializeToKtx2(
            static_cast<const ImageAssetData&>(compiled), identity);
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

        ctx.compiledData = m_compiler.Compile(ctx.id, *ctx.rawData, ctx.subAssets);
    }
}
