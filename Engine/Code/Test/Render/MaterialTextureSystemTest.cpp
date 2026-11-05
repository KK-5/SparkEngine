#include <gtest/gtest.h>

#include <ECS/WorldContext.h>

#include <Binding/Material/MaterialBinding.h>
#include <Material/MaterialTextureSystem.h>
#include <Material/MaterialContext.h>
#include <Material/MaterialUtils.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/System/RHIHandleClearSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAssetCompiler.h>

#include "FakeDevice.h"
#include "FakeHostBufferFactory.h"

using namespace Spark;
using namespace Spark::Render;

namespace
{
    class TextureImage final : public RHI::Image {};

    class TextureImagePool final : public RHI::ImagePool
    {
    public:
        bool m_failImage = false;
    private:
        RHI::ResultCode InitInternal(RHI::Device&, const RHI::ImagePoolDescriptor&) override
        {
            return RHI::ResultCode::Success;
        }
        RHI::ResultCode InitImageInternal(const RHI::ImageInitRequest&) override
        {
            return m_failImage ? RHI::ResultCode::Fail : RHI::ResultCode::Success;
        }
    };

    class TextureFactory final : public Spark::Test::FakeHostBufferFactory
    {
    public:
        Ptr<TextureImagePool> m_pool;
        Ptr<RHI::ImagePool> CreateImagePool() override
        {
            m_pool = new TextureImagePool;
            return m_pool;
        }
        Ptr<RHI::Image> CreateImage() override { return Ptr<RHI::Image>(new TextureImage); }
    };

    class TextureAssets final : public Service<Resource::AssetManager>::Handler
    {
    public:
        eastl::vector<Ptr<Resource::ImageAsset>> m_images;
        mutable uint32_t m_finds = 0;
        Ptr<Resource::Asset> FindAsset(const Resource::AssetId& id) const override
        {
            ++m_finds;
            for (const auto& image : m_images)
            {
                if (image->GetAssetId() == id)
                {
                    return image;
                }
            }
            return {};
        }
        Ptr<Resource::Asset> LoadAsset(const Resource::AssetId&) override { throw std::logic_error("Unexpected load"); }
        Ptr<Resource::Asset> RequestAsset(const Resource::AssetId&) override { throw std::logic_error("Unexpected load"); }
        Resource::AssetId MakeAssetId(eastl::string_view) override { return {}; }
        eastl::vector<Resource::AssetId> GetRegisteredAssetIds() const override { return {}; }
        Resource::AssetType GetSupportAssetType(eastl::string_view) override { return Resource::AssetType::Image; }
        void AssetRegistry() override {}
        Resource::AssetId SaveAsset(const Resource::Asset&, eastl::string_view) override { return {}; }
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "TestTextureAssets"_hs; }
    private:
        void InitInternal() override {}
        void ShutdownInternal() override {}
        void ReleaseAsset(const Resource::AssetId&, const Resource::Asset*) override {}
    };
}

class MaterialTextureSystemTest : public ::testing::Test
{
protected:
    WorldContext world;
    Material::MaterialContext materials;
    RHI::RHIContext rhi;
    RHI::RHIEventContext events;
    TextureFactory factory;
    TextureAssets assets;
    Ptr<RHI::PhysicalDevice> physical{ new RHI::PhysicalDevice };
    Ptr<Spark::Test::FakeDevice> device{ new Spark::Test::FakeDevice };
    MaterialTextureSystem textures;

    void SetUp() override
    {
        WorldExecuteContext::Push(world);
        Material::MaterialExecuteContext::Push(materials);
        RHI::RHIExecuteContext::Push(rhi);
        RHI::RHIEventExecuteContext::Push(events);
        ASSERT_EQ(device->Init(*physical, RHI::DeviceDescriptor{}), RHI::ResultCode::Success);
        textures.Init(*device);
    }

    void TearDown() override
    {
        events.Clear();
        textures.Shutdown();
        RHI::RHIHandleClearSystem::Collect(rhi);
        device->FlushReleaseQueue();
        device->BusDisconnect();
        RHI::RHIEventExecuteContext::Pop();
        RHI::RHIExecuteContext::Pop();
        Material::MaterialExecuteContext::Pop();
        WorldExecuteContext::Pop();
    }

    static void MakeReady(Resource::ImageAsset& asset)
    {
        Resource::ImageAssetRawData raw(1, 1, Resource::ImageFormat::RGBA8,
            { 255, 255, 255, 255 }, asset.GetAssetId().GetPath());
        Resource::ImageAssetCompiler compiler;
        eastl::vector<Resource::SubAssetEntry> children;
        asset.SetDataReady(compiler.Compile(asset.GetAssetId(), raw, children));
    }

    Ptr<Resource::ImageAsset> AddImage(const char* path, bool ready = true,
        Resource::ImageColorSpace colorSpace = Resource::ImageColorSpace::Linear)
    {
        Resource::ImageAssetDescriptor descriptor;
        descriptor.compression = Resource::TextureCompression::None;
        descriptor.colorSpace = colorSpace;
        descriptor.maxMipLevels = 1;
        const auto id = Resource::AssetId::Of<Resource::ImageAsset>(path, descriptor);
        Ptr<Resource::ImageAsset> asset = new Resource::ImageAsset(id);
        if (ready)
        {
            MakeReady(*asset);
        }
        assets.m_images.push_back(asset);
        return asset;
    }

    Material::MaterialHandle AddMaterial(const Resource::AssetId& id)
    {
        Resource::StandardPBR params;
        params.m_textures[0] = id;
        return Material::CreateMaterial(materials, params);
    }

    RHI::RHIHandle Texture(Material::MaterialHandle material, size_t slot = 0)
    {
        return materials.Get<MaterialGPUTextures>(material).m_handles[slot];
    }
};

TEST_F(MaterialTextureSystemTest, SharedSlotsAndMaterialsUploadOnceAndReuseTheCache)
{
    const auto asset = AddImage("test://shared");
    const auto first = AddMaterial(asset->GetAssetId());
    const auto second = AddMaterial(asset->GetAssetId());
    materials.Get<Resource::StandardPBR>(first).m_textures[1] = asset->GetAssetId();
    textures.Update();
    const auto image = Texture(first);
    ASSERT_NE(image, RHI::NullHandle);
    EXPECT_EQ(Texture(first, 1), image);
    EXPECT_EQ(Texture(second), image);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 1u);
    EXPECT_EQ(rhi.GetView<RHI::PendingImageInit>().size(), 0u);
    EXPECT_EQ(rhi.GetView<RHI::PerFrameTag>().size(), 0u);
    EXPECT_TRUE(rhi.Has<RHI::Components::Image>(image));
    EXPECT_TRUE(rhi.Has<MaterialTextureTag>(image));
    EXPECT_FALSE(RHI::IsResourceReady(rhi, image));
    EXPECT_EQ(factory.m_pool->GetDescriptor().m_heapMemoryLevel, RHI::HeapMemoryLevel::Device);
    const auto lookups = assets.m_finds;
    textures.Update();
    EXPECT_EQ(assets.m_finds, lookups);
    EXPECT_EQ(Texture(first), image);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 1u);
}

TEST_F(MaterialTextureSystemTest, DifferentDescriptorVariantsOfOneFileStayDistinct)
{
    const auto linear = AddImage("test://variant");
    const auto srgb = AddImage("test://variant", true, Resource::ImageColorSpace::sRGB);
    const auto first = AddMaterial(linear->GetAssetId());
    const auto second = AddMaterial(srgb->GetAssetId());
    textures.Update();
    ASSERT_NE(Texture(first), RHI::NullHandle);
    ASSERT_NE(Texture(second), RHI::NullHandle);
    EXPECT_NE(Texture(first), Texture(second));
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 2u);
}

TEST_F(MaterialTextureSystemTest, TheLastLiveReferenceRetiresTheImage)
{
    const auto asset = AddImage("test://shared");
    const auto first = AddMaterial(asset->GetAssetId());
    const auto second = AddMaterial(asset->GetAssetId());
    textures.Update();
    const auto image = Texture(first);
    materials.Get<Resource::StandardPBR>(first).m_textures[0] = {};
    textures.Update();
    EXPECT_EQ(Texture(first), RHI::NullHandle);
    EXPECT_FALSE(rhi.Has<DeadTag>(image));
    materials.Add<DeadTag>(second);
    textures.Update();
    EXPECT_TRUE(rhi.Valid(image));
    EXPECT_TRUE(rhi.Has<DeadTag>(image));
    RHI::RHIHandleClearSystem::Collect(rhi);
    EXPECT_FALSE(rhi.Valid(image));
}

TEST_F(MaterialTextureSystemTest, AnUnreadyAssetResolvesLaterWithoutRewritingTheMaterial)
{
    const auto asset = AddImage("test://late", false);
    const auto material = AddMaterial(asset->GetAssetId());
    textures.Update();
    EXPECT_EQ(Texture(material), RHI::NullHandle);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 0u);
    MakeReady(*asset);
    textures.Update();
    EXPECT_NE(Texture(material), RHI::NullHandle);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 1u);
}

TEST_F(MaterialTextureSystemTest, FailedImageCreationCanBeRetried)
{
    const auto material = AddMaterial(AddImage("test://retry")->GetAssetId());
    factory.m_pool->m_failImage = true;
    textures.Update();
    EXPECT_EQ(Texture(material), RHI::NullHandle);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 0u);
    factory.m_pool->m_failImage = false;
    textures.Update();
    EXPECT_NE(Texture(material), RHI::NullHandle);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 1u);
}

TEST_F(MaterialTextureSystemTest, UploadSourceOutlivesAllMaterialAndImageReferences)
{
    const auto asset = AddImage("test://source-lifetime");
    const auto material = AddMaterial(asset->GetAssetId());
    textures.Update();
    const auto image = Texture(material);
    const auto request = events.GetView<RHI::ImageUploadRequest>().front();
    materials.DestoryEntity(material);
    textures.Update();
    EXPECT_TRUE(rhi.Has<DeadTag>(image));
    RHI::RHIHandleClearSystem::Collect(rhi);
    assets.m_images.clear();
    EXPECT_EQ(asset->UseCount(), 2u); // The test and the upload request.
    EXPECT_EQ(events.Get<RHI::ImageUploadRequest>(request).m_data, asset->GetImageData()->GetTextureBytes().data());
    RHI::FinishUploadRequest(rhi, events, request);
    EXPECT_EQ(asset->UseCount(), 1u);
}

TEST_F(MaterialTextureSystemTest, OverrideEditsResolveOnTheNextTextureUpdate)
{
    const auto first = AddImage("test://override-a");
    const auto second = AddImage("test://override-b");
    const auto entity = world.CreateEntity();
    Material::StandardPBROverride params;
    params.m_textures[0] = first->GetAssetId();
    world.Add<Material::StandardPBROverride>(entity, params);
    textures.Update();
    SyncOverrideMaterials();
    const auto material = world.Get<MaterialOverrideRef>(entity).m_material;
    EXPECT_FALSE(materials.Has<MaterialGPUTextures>(material));
    textures.Update();
    const auto oldImage = Texture(material);
    ASSERT_NE(oldImage, RHI::NullHandle);

    world.Get<Material::StandardPBROverride>(entity).m_textures[0] = second->GetAssetId();
    textures.Update();
    SyncOverrideMaterials();
    EXPECT_EQ(Texture(material), oldImage);
    textures.Update();
    const auto newImage = Texture(material);
    EXPECT_NE(newImage, oldImage);
    EXPECT_NE(newImage, RHI::NullHandle);
    EXPECT_TRUE(rhi.Has<DeadTag>(oldImage));

    world.Remove<Material::StandardPBROverride>(entity);
    textures.Update();
    SyncOverrideMaterials();
    ReapDeadMaterials();
    EXPECT_FALSE(materials.Valid(material));
    textures.Update();
    EXPECT_TRUE(rhi.Has<DeadTag>(newImage));
}

TEST_F(MaterialTextureSystemTest, ShutdownRetiresImagesBeforeThePoolCloses)
{
    const auto material = AddMaterial(AddImage("test://shutdown")->GetAssetId());
    textures.Update();
    const auto image = Texture(material);
    events.Clear();
    textures.Shutdown();
    EXPECT_EQ(materials.GetView<MaterialGPUTextures>().size(), 0u);
    EXPECT_TRUE(rhi.Valid(image));
    EXPECT_TRUE(rhi.Has<DeadTag>(image));
    EXPECT_TRUE(factory.m_pool->IsInitialized());
    RHI::RHIHandleClearSystem::Collect(rhi);
    EXPECT_FALSE(rhi.Valid(image));
}
