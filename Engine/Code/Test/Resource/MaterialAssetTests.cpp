#include <gtest/gtest.h>

#include <Resource/AssetManager.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Material/MaterialAsset.h>
#include <VFS/MountTable.h>
#include <VFS/VFSSystem.h>

using namespace Spark;
using namespace Spark::Resource;

namespace
{
    constexpr size_t kBaseColor = static_cast<size_t>(MaterialTexSlot::BaseColor);
    constexpr size_t kNormal    = static_cast<size_t>(MaterialTexSlot::Normal);
}

//! No cache:// mount: a material has no cache format, so every case here goes through the
//! loader and the compiler.
class MaterialAssetTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_vfs = CreateSystem<VFSSystem>();
        m_vfs->Init();
        m_vfs->Mount("test", TEST_RESOURCE_DIR);

        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
    }

    void TearDown() override
    {
        m_assetManager.reset();
        m_vfs.reset();
    }

    //! Through the interface on purpose: SparkAssetManager's LoadAsset override hides the
    //! typed LoadAsset<T> template that lives on AssetManager.
    Ptr<MaterialAsset> Load(const char* virtualPath)
    {
        AssetManager& manager = *m_assetManager;
        return manager.LoadAsset<MaterialAsset>(AssetId::Of<MaterialAsset>(virtualPath));
    }

    SystemUniquePtr<VFSSystem>         m_vfs;
    SystemUniquePtr<SparkAssetManager> m_assetManager;
};

TEST_F(MaterialAssetTestFixture, EveryAuthoredValueSurvivesTheRead)
{
    Ptr<MaterialAsset> material = Load("test://Asset/Material/Wood.smat");
    ASSERT_TRUE(material);
    ASSERT_EQ(material->GetStatus(), AssetStatus::Ready);

    const MaterialAssetData* data = material->GetMaterialData();
    ASSERT_TRUE(data);

    const StandardPBR& params = data->GetParams();
    EXPECT_FLOAT_EQ(params.m_baseColor.r, 0.6f);
    EXPECT_FLOAT_EQ(params.m_baseColor.a, 1.0f);
    EXPECT_FLOAT_EQ(params.m_metallic, 0.1f);
    EXPECT_FLOAT_EQ(params.m_roughness, 0.8f);
    EXPECT_FLOAT_EQ(params.m_emissive.g, 0.5f);
    EXPECT_FLOAT_EQ(params.m_emissiveStrength, 2.0f);
    EXPECT_FLOAT_EQ(params.m_normalScale, 1.5f);
    EXPECT_FLOAT_EQ(params.m_occlusionStrength, 0.75f);

    // state is read the same way, from its own top-level key.
    EXPECT_EQ(data->GetState().m_alphaMode, AlphaMode::Mask);
    EXPECT_FLOAT_EQ(data->GetState().m_alphaCutoff, 0.25f);
    EXPECT_TRUE(data->GetState().m_doubleSided);

    // A null slot is "no map", not a failure -- the whole file reads clean with five of them.
    for (const AssetId& texture : params.m_textures)
    {
        EXPECT_FALSE(texture.IsValid());
    }
}

TEST_F(MaterialAssetTestFixture, MissingKeysAreDefaultsAndUnknownKeysAreIgnored)
{
    // Minimal.smat carries no state at all and one misspelled property. Both rules are the
    // format's version tolerance, and together they are the trap of a hand-written file:
    // "Roughnes" reports nothing and silently leaves Roughness at its default.
    Ptr<MaterialAsset> material = Load("test://Asset/Material/Minimal.smat");
    ASSERT_TRUE(material);
    ASSERT_EQ(material->GetStatus(), AssetStatus::Ready);

    const MaterialAssetData* data = material->GetMaterialData();
    ASSERT_TRUE(data);

    EXPECT_FLOAT_EQ(data->GetParams().m_roughness, StandardPBR{}.m_roughness);
    EXPECT_EQ(data->GetState().m_alphaMode, AlphaMode::Opaque);
    EXPECT_FLOAT_EQ(data->GetState().m_alphaCutoff, 0.5f);
    EXPECT_FALSE(data->GetState().m_doubleSided);
}

TEST_F(MaterialAssetTestFixture, TexturesAreLoadedAsDependencies)
{
    Ptr<MaterialAsset> material = Load("test://Asset/Material/Textured.smat");
    ASSERT_TRUE(material);
    ASSERT_EQ(material->GetStatus(), AssetStatus::Ready);

    const StandardPBR& params = material->GetMaterialData()->GetParams();

    // The same file in two usages is two assets. The hand-written `desc` spells out only
    // what differs from the defaults, and still has to land on exactly the id the engine
    // builds for that usage -- otherwise a `.smat` could not pin a texture's colour space.
    const char* path = "test://Asset/Textures/stone_wall_04_diff_1k.jpg";
    EXPECT_EQ(params.m_textures[kBaseColor],
              AssetId::Of(path, {}, AssetType::Image,
                          ImageAsset::DescriptorForUsage(ImageUsage::Texture2D)));
    EXPECT_EQ(params.m_textures[kNormal],
              AssetId::Of(path, {}, AssetType::Image,
                          ImageAsset::DescriptorForUsage(ImageUsage::NormalMap)));
    EXPECT_NE(params.m_textures[kBaseColor], params.m_textures[kNormal]);

    // Declared as dependencies, so both are loaded before the material goes Ready. Nothing
    // else in this test asked for them.
    for (const size_t slot : {kBaseColor, kNormal})
    {
        Ptr<Asset> texture = m_assetManager->FindAsset(params.m_textures[slot]);
        ASSERT_TRUE(texture) << "slot " << slot;
        EXPECT_EQ(texture->GetStatus(), AssetStatus::Ready) << "slot " << slot;
    }
}

TEST_F(MaterialAssetTestFixture, AnUnknownShadingModelIsAnError)
{
    // Not a fall back to StandardPBR: another model's properties read as StandardPBR's
    // would keep the names that happen to match and drop the rest, which is a plausible
    // wrong material rather than a visible failure.
    Ptr<MaterialAsset> material = Load("test://Asset/Material/UnknownModel.smat");
    ASSERT_TRUE(material);
    EXPECT_EQ(material->GetStatus(), AssetStatus::Error);
}

TEST_F(MaterialAssetTestFixture, MalformedJsonIsAnError)
{
    Ptr<MaterialAsset> material = Load("test://Asset/Material/Broken.smat");
    ASSERT_TRUE(material);
    EXPECT_EQ(material->GetStatus(), AssetStatus::Error);
}

TEST_F(MaterialAssetTestFixture, ASubAssetTextureIsRejectedByTheMaterialThatNamesIt)
{
    // A sub-asset's bytes live inside its parent's file, so it cannot be loaded on its own.
    // Left to ProcessAsset the error would name the model; caught here it names the .smat.
    Ptr<MaterialAsset> material = Load("test://Asset/Material/SubAssetTexture.smat");
    ASSERT_TRUE(material);
    EXPECT_EQ(material->GetStatus(), AssetStatus::Error);
}

TEST_F(MaterialAssetTestFixture, AMissingFileIsAnError)
{
    Ptr<MaterialAsset> material = Load("test://Asset/Material/NoSuchThing.smat");
    ASSERT_TRUE(material);
    EXPECT_EQ(material->GetStatus(), AssetStatus::Error);
}
