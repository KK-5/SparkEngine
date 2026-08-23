#include <gtest/gtest.h>

#include <Resource/AssetTypes.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Shader/ShaderAsset.h>

using namespace Spark;
using namespace Spark::Resource;

TEST(AssetIdTypeTest, OfCarriesTheTypeOfT)
{
    EXPECT_EQ(AssetId::Of<ImageAsset>("test://Asset/a.png").GetAssetType(),  AssetType::Image);
    EXPECT_EQ(AssetId::Of<ModelAsset>("test://Asset/a.glb").GetAssetType(),  AssetType::Model);
    EXPECT_EQ(AssetId::Of<ShaderAsset>("test://Asset/a.hlsl").GetAssetType(), AssetType::Shader);
}

TEST(AssetIdTypeTest, OfSubCarriesTheSubAssetType)
{
    const AssetId sub = AssetId::OfSub<ImageAsset>("test://Asset/a.glb", "image/3");
    EXPECT_EQ(sub.GetAssetType(), AssetType::Image);
    EXPECT_TRUE(sub.IsSubAsset());
}

// The parent is a Model and the sub-asset is an Image, so the type cannot be recovered
// from the path -- carrying it is the only way the sub id stays self-describing.
TEST(AssetIdTypeTest, SubAssetTypeDisagreesWithItsPathExtension)
{
    const AssetId parent = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb");
    const AssetId sub    = ImageAsset::MakeSubId(parent, "image/0", ImageUsage::Texture2D);
    EXPECT_EQ(parent.GetAssetType(), AssetType::Model);
    EXPECT_EQ(sub.GetAssetType(),    AssetType::Image);
}

TEST(AssetIdTypeTest, TypeSeparatesOtherwiseIdenticalIds)
{
    const eastl::string_view path = "test://Asset/same.bin";
    const AssetId asImage  = AssetId::Of(path, {}, AssetType::Image,  ImageAsset::DefaultDescriptor());
    const AssetId asModel  = AssetId::Of(path, {}, AssetType::Model,  ImageAsset::DefaultDescriptor());
    EXPECT_NE(asImage, asModel);
    EXPECT_NE(asImage.GetHash(), asModel.GetHash());
}

TEST(AssetIdTypeTest, WithDescriptorKeepsTheType)
{
    const AssetId base = AssetId::Of<ImageAsset>("test://Asset/a.png");
    const AssetId normal = base.WithDescriptor(ImageAsset::DescriptorForUsage(ImageUsage::NormalMap));
    EXPECT_EQ(normal.GetAssetType(), AssetType::Image);
    EXPECT_NE(normal, base);
}

TEST(AssetIdTypeTest, DefaultIdHasNoType)
{
    const AssetId id;
    EXPECT_EQ(id.GetAssetType(), AssetType::Unknown);
    EXPECT_FALSE(id.IsValid());
}
