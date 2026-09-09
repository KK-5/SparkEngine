#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <Service/Service.h>

#include <Resource/AssetLoadBatch.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Material/MaterialAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Shader/ShaderAsset.h>

using namespace Spark;
using namespace Spark::Resource;

namespace
{
    //! Statuses the test drives by hand: the real manager would need a FileSystem, four
    //! builders and a worker thread to say the same things.
    class FakeAssetManager final : public AssetManager
    {
    public:
        FakeAssetManager()
        {
            Service<AssetManager>::Register(this);
        }

        ~FakeAssetManager() override
        {
            Service<AssetManager>::Unregister(this);
        }

        void Add(const AssetId& id, AssetStatus status)
        {
            Ptr<Asset> asset(new Asset(id));
            asset->SetStatus(status);
            m_assets.push_back(eastl::move(asset));
        }

        void SetStatus(const AssetId& id, AssetStatus status)
        {
            if (Ptr<Asset> asset = FindAsset(id))
            {
                asset->SetStatus(status);
            }
        }

        const eastl::vector<AssetId>& Requested() const { return m_requested; }

        // ISystem
        eastl::vector<HashString> Request() const override { return {}; }
        HashString                GetName() const override { return "FakeAssetManager"_hs; }

        // AssetManager
        Ptr<Asset> LoadAsset(const AssetId& id) override { return FindAsset(id); }

        Ptr<Asset> RequestAsset(const AssetId& id) override
        {
            m_requested.push_back(id);
            return FindAsset(id);
        }

        Ptr<Asset> FindAsset(const AssetId& id) const override
        {
            for (const Ptr<Asset>& asset : m_assets)
            {
                if (asset->GetAssetId() == id)
                {
                    return asset;
                }
            }
            return nullptr;
        }

        eastl::vector<AssetId> GetRegisteredAssetIds() const override
        {
            eastl::vector<AssetId> ids;
            for (const Ptr<Asset>& asset : m_assets)
            {
                if (!asset->GetAssetId().IsSubAsset())
                {
                    ids.push_back(asset->GetAssetId());
                }
            }
            return ids;
        }

        AssetType GetSupportAssetType(eastl::string_view) override { return AssetType::Unknown; }
        AssetId   MakeAssetId(eastl::string_view) override { return AssetId(); }
        void      AssetRegistry() override {}
        AssetId   SaveAsset(const Asset&, eastl::string_view) override { return AssetId(); }

    protected:
        void ReleaseAsset(const AssetId&, const Asset*) override {}

    private:
        void InitInternal() override {}
        void ShutdownInternal() override {}

        eastl::vector<Ptr<Asset>> m_assets;
        eastl::vector<AssetId>    m_requested;
    };

    AssetId ImageId(const char* path)
    {
        return AssetId::Of(path, {}, AssetType::Image,
                           ImageAsset::DescriptorForUsage(ImageUsage::Texture2D));
    }

    const AssetLoadProgress::Entry* FindEntry(const AssetLoadProgress& progress, AssetType type)
    {
        for (const AssetLoadProgress::Entry& entry : progress.entries)
        {
            if (entry.type == type)
            {
                return &entry;
            }
        }
        return nullptr;
    }
}

class AssetLoadBatchTestFixture : public ::testing::Test
{
protected:
    eastl::vector<AssetId> Ids() const
    {
        return m_manager.GetRegisteredAssetIds();
    }

    FakeAssetManager m_manager;
};

TEST_F(AssetLoadBatchTestFixture, IsNotCompleteBeforeRequestAll)
{
    m_manager.Add(ImageId("test://A.png"), AssetStatus::Ready);

    const AssetLoadBatch batch(Ids());

    EXPECT_FALSE(batch.IsComplete());

    const AssetLoadProgress progress = batch.GetProgress();
    EXPECT_EQ(progress.total, 1u);
    EXPECT_EQ(progress.ready, 0u);
    EXPECT_FALSE(progress.complete);
    EXPECT_TRUE(m_manager.Requested().empty());
}

TEST_F(AssetLoadBatchTestFixture, AnEmptyBatchCompletesOnceRequested)
{
    AssetLoadBatch batch({});

    EXPECT_FALSE(batch.IsComplete());
    batch.RequestAll();
    EXPECT_TRUE(batch.IsComplete());
}

//! Why progress is polled: RequestAsset returns an already-Ready asset without announcing
//! it, so a batch waiting on OnAssetReady would never finish. Startup has plenty of these.
TEST_F(AssetLoadBatchTestFixture, AssetsAlreadyReadyAreCounted)
{
    m_manager.Add(ImageId("test://A.png"), AssetStatus::Ready);
    m_manager.Add(ImageId("test://B.png"), AssetStatus::Ready);

    AssetLoadBatch batch(Ids());
    batch.RequestAll();

    const AssetLoadProgress progress = batch.GetProgress();
    EXPECT_EQ(progress.ready, 2u);
    EXPECT_EQ(progress.total, 2u);
    EXPECT_TRUE(progress.complete);
    EXPECT_TRUE(batch.IsComplete());
}

TEST_F(AssetLoadBatchTestFixture, CountsAreSplitPerAssetType)
{
    m_manager.Add(ImageId("test://A.png"), AssetStatus::Ready);
    m_manager.Add(ImageId("test://B.png"), AssetStatus::NotLoaded);
    m_manager.Add(AssetId::Of<ShaderAsset>("test://S.hlsl"), AssetStatus::Ready);
    m_manager.Add(AssetId::Of<ModelAsset>("test://M.glb"), AssetStatus::Error);

    AssetLoadBatch batch(Ids());
    batch.RequestAll();

    const AssetLoadProgress progress = batch.GetProgress();

    const AssetLoadProgress::Entry* image  = FindEntry(progress, AssetType::Image);
    const AssetLoadProgress::Entry* shader = FindEntry(progress, AssetType::Shader);
    const AssetLoadProgress::Entry* model  = FindEntry(progress, AssetType::Model);

    ASSERT_NE(image, nullptr);
    ASSERT_NE(shader, nullptr);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(FindEntry(progress, AssetType::Material), nullptr);

    EXPECT_EQ(image->total, 2u);
    EXPECT_EQ(image->ready, 1u);
    EXPECT_EQ(shader->ready, 1u);
    EXPECT_EQ(model->failed, 1u);

    EXPECT_EQ(progress.total, 4u);
    EXPECT_EQ(progress.ready, 2u);
    EXPECT_EQ(progress.failed, 1u);
    EXPECT_FALSE(progress.complete);
}

TEST_F(AssetLoadBatchTestFixture, EntriesAreOrderedByAssetType)
{
    m_manager.Add(AssetId::Of<MaterialAsset>("test://M.smat"), AssetStatus::Ready);
    m_manager.Add(AssetId::Of<ShaderAsset>("test://S.hlsl"), AssetStatus::Ready);
    m_manager.Add(ImageId("test://A.png"), AssetStatus::Ready);

    AssetLoadBatch batch(Ids());
    batch.RequestAll();

    const AssetLoadProgress progress = batch.GetProgress();
    ASSERT_EQ(progress.entries.size(), 3u);
    for (size_t i = 1; i < progress.entries.size(); ++i)
    {
        EXPECT_LT(static_cast<uint32_t>(progress.entries[i - 1].type),
                  static_cast<uint32_t>(progress.entries[i].type));
    }
}

//! The welcome screen has to let the user in even when something will not build.
TEST_F(AssetLoadBatchTestFixture, ErrorsCompleteTheBatch)
{
    m_manager.Add(ImageId("test://A.png"), AssetStatus::Error);
    m_manager.Add(ImageId("test://B.png"), AssetStatus::Ready);

    AssetLoadBatch batch(Ids());
    batch.RequestAll();

    const AssetLoadProgress progress = batch.GetProgress();
    EXPECT_EQ(progress.ready, 1u);
    EXPECT_EQ(progress.failed, 1u);
    EXPECT_TRUE(progress.complete);
    EXPECT_TRUE(batch.IsComplete());
}

TEST_F(AssetLoadBatchTestFixture, CurrentNamesAnAssetBeingBuilt)
{
    const AssetId loading = ImageId("test://B.png");
    m_manager.Add(ImageId("test://A.png"), AssetStatus::Ready);
    m_manager.Add(loading, AssetStatus::Compiling);

    AssetLoadBatch batch(Ids());
    batch.RequestAll();

    AssetLoadProgress progress = batch.GetProgress();
    EXPECT_EQ(progress.current, loading.GetPath());
    EXPECT_FALSE(progress.complete);

    // Queued is waiting in line, not being worked on.
    m_manager.SetStatus(loading, AssetStatus::Queued);
    progress = batch.GetProgress();
    EXPECT_TRUE(progress.current.empty());
    EXPECT_FALSE(progress.complete);

    m_manager.SetStatus(loading, AssetStatus::Ready);
    progress = batch.GetProgress();
    EXPECT_TRUE(progress.current.empty());
    EXPECT_TRUE(progress.complete);
}

TEST_F(AssetLoadBatchTestFixture, RequestAllAsksOnceForEachId)
{
    m_manager.Add(ImageId("test://A.png"), AssetStatus::NotLoaded);
    m_manager.Add(ImageId("test://B.png"), AssetStatus::NotLoaded);

    AssetLoadBatch batch(Ids());
    batch.RequestAll();
    batch.RequestAll();

    EXPECT_EQ(m_manager.Requested().size(), 2u);
}

TEST_F(AssetLoadBatchTestFixture, AnUncreatableAssetCountsAsFailed)
{
    eastl::vector<AssetId> ids;
    ids.push_back(ImageId("test://Missing.png"));   // never Add()ed

    AssetLoadBatch batch(eastl::move(ids));
    batch.RequestAll();

    const AssetLoadProgress progress = batch.GetProgress();
    EXPECT_EQ(progress.failed, 1u);
    EXPECT_TRUE(batch.IsComplete());
}

//! Rescan is what makes this matter: by then every glb has put dozens of sub-assets in
//! the database, and ProcessAsset refuses to build one alone.
TEST_F(AssetLoadBatchTestFixture, SubAssetsAreNotOfferedForLoading)
{
    const AssetId root = AssetId::Of<ModelAsset>("test://M.glb");
    m_manager.Add(root, AssetStatus::Ready);
    m_manager.Add(ImageAsset::MakeSubId(root, "image/0", ImageUsage::Texture2D),
                  AssetStatus::Ready);

    const eastl::vector<AssetId> ids = Ids();

    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids.front(), root);
}
