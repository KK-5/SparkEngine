#include <gtest/gtest.h>

#include <filesystem>

#include <Resource/AssetManager.h>
#include <Resource/Bus/AssetBus.h>
#include <Resource/Bus/AssetBuildBus.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Material/MaterialAsset.h>
#include <Resource/Material/MaterialAssetCompiler.h>
#include <Resource/Material/MaterialAssetWriter.h>
#include <Resource/Material/MaterialRawTypes.h>
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

//! The write side. Straight back through the compiler rather than through a file: what is
//! under test is the format, and a temporary on disk would only add a way to fail.
TEST_F(MaterialAssetTestFixture, WritingAMaterialBackRoundTrips)
{
    for (const char* path : {"test://Asset/Material/Wood.smat",
                             "test://Asset/Material/Textured.smat"})
    {
        Ptr<MaterialAsset> material = Load(path);
        ASSERT_TRUE(material) << path;
        ASSERT_EQ(material->GetStatus(), AssetStatus::Ready) << path;

        const MaterialAssetData* first = material->GetMaterialData();
        ASSERT_TRUE(first) << path;

        const eastl::vector<uint8_t> written = WriteMaterialAsset(*first);
        ASSERT_FALSE(written.empty()) << path;

        UniquePtr<AssetData> reread = MaterialAssetCompiler{}.Compile(
            AssetId::Of<MaterialAsset>(path), MaterialEncodedRawData(written));
        ASSERT_TRUE(reread) << path;

        // Bytes, not a parsed document: the assertion says nothing about which format the
        // writer chose, only that a second pass produces the same file.
        EXPECT_EQ(written, WriteMaterialAsset(static_cast<const MaterialAssetData&>(*reread)))
            << path;
    }
}

//! Every authored value reaches the file. The round trip above cannot see this on its own:
//! a field the writer omits comes back as its default, and if it started at the default the
//! two passes still agree. So every field here is set AWAY from its default -- omitting one
//! then shows up as a value that changed.
TEST_F(MaterialAssetTestFixture, NoAuthoredValueIsLostOnTheWayOut)
{
    StandardPBR params;
    params.m_baseColor         = {0.11f, 0.22f, 0.33f, 0.44f};
    params.m_metallic          = 0.15f;
    params.m_roughness         = 0.25f;
    params.m_specular          = 0.35f;
    params.m_emissive          = {0.55f, 0.66f, 0.77f, 0.88f};
    params.m_emissiveStrength  = 3.5f;
    params.m_normalScale       = 1.25f;
    params.m_occlusionStrength = 0.45f;

    // A distinct id per slot, so writing the same one into every slot fails too. These paths
    // never have to exist: only the writer and the compiler run here, and neither loads a
    // texture -- that is the builder's half.
    const char* texturePaths[MaterialTexSlotCount] = {
        "test://Slot/A.png", "test://Slot/B.png", "test://Slot/C.png",
        "test://Slot/D.png", "test://Slot/E.png",
    };
    for (size_t slot = 0; slot < MaterialTexSlotCount; ++slot)
    {
        params.m_textures[slot] = AssetId::Of(texturePaths[slot], {}, AssetType::Image,
                                              ImageAsset::DescriptorForUsage(ImageUsage::Texture2D));
    }

    MaterialState state;
    state.m_alphaMode   = AlphaMode::Blend;
    state.m_alphaCutoff = 0.375f;
    state.m_doubleSided = true;

    const eastl::vector<uint8_t> written = WriteMaterialAsset(MaterialAssetData(params, state));
    ASSERT_FALSE(written.empty());

    UniquePtr<AssetData> reread = MaterialAssetCompiler{}.Compile(
        AssetId::Of<MaterialAsset>("test://Asset/Material/Written.smat"),
        MaterialEncodedRawData(written));
    ASSERT_TRUE(reread);

    const StandardPBR&   back      = static_cast<const MaterialAssetData&>(*reread).GetParams();
    const MaterialState& backState = static_cast<const MaterialAssetData&>(*reread).GetState();

    EXPECT_FLOAT_EQ(back.m_baseColor.r, params.m_baseColor.r);
    EXPECT_FLOAT_EQ(back.m_baseColor.g, params.m_baseColor.g);
    EXPECT_FLOAT_EQ(back.m_baseColor.b, params.m_baseColor.b);
    EXPECT_FLOAT_EQ(back.m_baseColor.a, params.m_baseColor.a);
    EXPECT_FLOAT_EQ(back.m_metallic, params.m_metallic);
    EXPECT_FLOAT_EQ(back.m_roughness, params.m_roughness);
    EXPECT_FLOAT_EQ(back.m_specular, params.m_specular);
    EXPECT_FLOAT_EQ(back.m_emissive.r, params.m_emissive.r);
    EXPECT_FLOAT_EQ(back.m_emissive.g, params.m_emissive.g);
    EXPECT_FLOAT_EQ(back.m_emissive.b, params.m_emissive.b);
    EXPECT_FLOAT_EQ(back.m_emissive.a, params.m_emissive.a);
    EXPECT_FLOAT_EQ(back.m_emissiveStrength, params.m_emissiveStrength);
    EXPECT_FLOAT_EQ(back.m_normalScale, params.m_normalScale);
    EXPECT_FLOAT_EQ(back.m_occlusionStrength, params.m_occlusionStrength);

    for (size_t slot = 0; slot < MaterialTexSlotCount; ++slot)
    {
        EXPECT_EQ(back.m_textures[slot], params.m_textures[slot]) << "slot " << slot;
    }

    EXPECT_EQ(backState.m_alphaMode, state.m_alphaMode);
    EXPECT_FLOAT_EQ(backState.m_alphaCutoff, state.m_alphaCutoff);
    EXPECT_EQ(backState.m_doubleSided, state.m_doubleSided);
}

// ============================================================================
// Saving through the manager. Needs a writable mount.
// ============================================================================

//! SaveAsset end to end: PrepareToSave → Serialize → WriteAssetFile → OnAssetSaved.
//! Material is the only type that answers the first with anything but "go ahead".
class MaterialSaveTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path()
               / "SparkMaterialSave"
               / ::testing::UnitTest::GetInstance()->current_test_info()->name();

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);

        m_vfs = CreateSystem<VFSSystem>();
        m_vfs->Init();
        m_vfs->Mount("test", TEST_RESOURCE_DIR);

        // Not watched: every event would be this test's own writes coming back.
        m_vfs->Mount("save", eastl::string(m_root.generic_string().c_str()), false);

        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
    }

    void TearDown() override
    {
        m_assetManager.reset();
        m_vfs.reset();

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    //! A material that exists only to be saved -- no path, so no id and no database entry.
    //! The shape the editor builds for Save As and New.
    Ptr<MaterialAsset> Unregistered(const StandardPBR& params, const MaterialState& state = {})
    {
        Ptr<MaterialAsset> asset(new MaterialAsset(AssetId()));
        asset->SetDataReady(MakeUnique<MaterialAssetData>(params, state));
        return asset;
    }

    AssetManager& Manager() { return *m_assetManager; }

    std::filesystem::path              m_root;
    SystemUniquePtr<VFSSystem>         m_vfs;
    SystemUniquePtr<SparkAssetManager> m_assetManager;
};

TEST_F(MaterialSaveTestFixture, ASavedMaterialReadsBackWithItsValues)
{
    StandardPBR params;
    params.m_baseColor = {0.2f, 0.4f, 0.6f, 1.0f};
    params.m_roughness = 0.375f;

    MaterialState state;
    state.m_doubleSided = true;

    const AssetId id = Manager().SaveAsset(*Unregistered(params, state), "save://Wood.smat");
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(id, AssetId::Of<MaterialAsset>("save://Wood.smat"));

    // Registered by the time SaveAsset returned: an editor action cannot wait for the
    // file watcher.
    ASSERT_TRUE(Manager().FindAsset(id));

    Ptr<MaterialAsset> reread = Manager().LoadAsset<MaterialAsset>(id);
    ASSERT_TRUE(reread);
    ASSERT_EQ(reread->GetStatus(), AssetStatus::Ready);

    const MaterialAssetData* data = reread->GetMaterialData();
    ASSERT_TRUE(data);
    EXPECT_FLOAT_EQ(data->GetParams().m_baseColor.b, 0.6f);
    EXPECT_FLOAT_EQ(data->GetParams().m_roughness, 0.375f);
    EXPECT_TRUE(data->GetState().m_doubleSided);
}

TEST_F(MaterialSaveTestFixture, SavingAnnouncesTheIdOnTheAssetBus)
{
    struct Listener : public AssetBus::Handler
    {
        void OnAssetSaved(const AssetId& id) override { m_saved.push_back(id); }
        eastl::vector<AssetId> m_saved;
    };

    Listener listener;
    listener.BusConnect(AssetType::Material);

    const AssetId id = Manager().SaveAsset(*Unregistered(StandardPBR{}), "save://Announced.smat");
    ASSERT_TRUE(id.IsValid());

    ASSERT_EQ(listener.m_saved.size(), 1u);
    EXPECT_EQ(listener.m_saved[0], id);

    listener.BusDisconnect();
}

TEST_F(MaterialSaveTestFixture, ATextureThatLivesInsideAModelRefusesTheSave)
{
    StandardPBR params;
    params.m_textures[kBaseColor] = MaterialAsset::MakeSubId(
        AssetId::Of("test://Asset/Model/Chair.glb", {}, AssetType::Model, nullptr), "image/3");

    const AssetId id = Manager().SaveAsset(*Unregistered(params), "save://Embedded.smat");
    EXPECT_FALSE(id.IsValid());

    // Nothing is written, not even a truncated file.
    EXPECT_FALSE(std::filesystem::exists(m_root / "Embedded.smat"));
}

TEST_F(MaterialSaveTestFixture, AnExtensionNothingBuildsIsRefused)
{
    const AssetId id = Manager().SaveAsset(*Unregistered(StandardPBR{}), "save://Wood.txt");
    EXPECT_FALSE(id.IsValid());
    EXPECT_FALSE(std::filesystem::exists(m_root / "Wood.txt"));
}

TEST_F(MaterialSaveTestFixture, AnAssetHoldingNoDataIsRefused)
{
    Ptr<MaterialAsset> empty(new MaterialAsset(AssetId()));

    const AssetId id = Manager().SaveAsset(*empty, "save://Empty.smat");
    EXPECT_FALSE(id.IsValid());
    EXPECT_FALSE(std::filesystem::exists(m_root / "Empty.smat"));
}

TEST_F(MaterialSaveTestFixture, SavingOverAnExistingMaterialKeepsItsId)
{
    StandardPBR first;
    first.m_roughness = 0.25f;
    const AssetId id = Manager().SaveAsset(*Unregistered(first), "save://Twice.smat");
    ASSERT_TRUE(id.IsValid());

    StandardPBR second;
    second.m_roughness = 0.75f;
    EXPECT_EQ(Manager().SaveAsset(*Unregistered(second), "save://Twice.smat"), id);

    // The file is the new one; the entry the first save registered is not asked to notice.
    UniquePtr<AssetData> onDisk;
    {
        eastl::vector<uint8_t> bytes;
        ASSERT_TRUE(Spark::Service<Spark::FileSystem>::Get()->ReadFile("save://Twice.smat", bytes));
        onDisk = MaterialAssetCompiler{}.Compile(id, MaterialEncodedRawData(bytes));
    }
    ASSERT_TRUE(onDisk);
    EXPECT_FLOAT_EQ(static_cast<const MaterialAssetData&>(*onDisk).GetParams().m_roughness, 0.75f);
}

//! Having a format is not the same as being cacheable: a material payload in some model's
//! unit would be written and never read back, since Deserialize is still declined.
TEST_F(MaterialSaveTestFixture, TheCacheGetsNoMaterialPayload)
{
    MaterialAssetData data(StandardPBR{}, MaterialState{});

    eastl::vector<uint8_t> forTheCache;
    AssetBuildBus::EventResult(forTheCache, AssetType::Material, &AssetBuildEvents::Serialize,
                               data, eastl::string_view("material:0"));
    EXPECT_TRUE(forTheCache.empty());

    eastl::vector<uint8_t> forAFile;
    AssetBuildBus::EventResult(forAFile, AssetType::Material, &AssetBuildEvents::Serialize,
                               data, eastl::string_view());
    EXPECT_FALSE(forAFile.empty());
}
