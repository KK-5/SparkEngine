#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Resource/AssetJsonSerializer.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Model/ModelAsset.h>

using namespace Spark;
using namespace Spark::Resource;

namespace
{
    Ptr<AssetDescriptor> NormalMapDescriptor()
    {
        ImageAssetDescriptor descriptor;
        descriptor.usage      = ImageUsage::NormalMap;
        descriptor.colorSpace = ImageColorSpace::Linear;
        return Ptr<AssetDescriptor>(new ImageAssetDescriptor(descriptor));
    }
}

TEST(AssetIdSerializeTests, TopLevelIdRoundTrips)
{
    const AssetId id = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb");

    JsonValue json;
    ASSERT_TRUE(AssetIdToJson(id, json));

    // sub is dropped by the same default-omission the descriptors get; desc is a plain
    // default here, so it collapses to {} and never reaches the file.
    EXPECT_EQ(json.dump(), R"({"type":"Model","path":"test://Asset/CubeTextured.glb"})");

    const AssetId decoded = AssetIdFromJson(json);
    EXPECT_EQ(decoded, id);
    EXPECT_EQ(decoded.GetHash(), id.GetHash());
    EXPECT_EQ(decoded.GetAssetType(), AssetType::Model);
}

TEST(AssetIdSerializeTests, SubAssetWithDescriptorRoundTrips)
{
    const AssetId id = AssetId::Of("test://Asset/CubeTextured.glb", "image/3",
                                   AssetType::Image, NormalMapDescriptor());

    JsonValue json;
    ASSERT_TRUE(AssetIdToJson(id, json));
    EXPECT_EQ(json.dump(),
        R"({"type":"Image","path":"test://Asset/CubeTextured.glb","sub":"image/3",)"
        R"("desc":{"colorSpace":"Linear","usage":"NormalMap"}})");

    const AssetId decoded = AssetIdFromJson(json);
    EXPECT_EQ(decoded, id);
    EXPECT_EQ(decoded.GetHash(), id.GetHash());
    EXPECT_TRUE(decoded.IsSubAsset());
    EXPECT_EQ(decoded.GetSubLabel(), "image/3");
}

TEST(AssetIdSerializeTests, DescriptorIsPartOfIdentity)
{
    // The same file as a normal map and as colour are two different assets, so the two
    // encodings must differ -- this is what dropping "desc" would silently collapse.
    const AssetId normalMap = AssetId::Of("test://Asset/a.png", {},
                                          AssetType::Image, NormalMapDescriptor());
    const AssetId colour    = AssetId::Of<ImageAsset>("test://Asset/a.png");
    ASSERT_NE(normalMap, colour);

    JsonValue normalJson;
    JsonValue colourJson;
    ASSERT_TRUE(AssetIdToJson(normalMap, normalJson));
    ASSERT_TRUE(AssetIdToJson(colour, colourJson));
    EXPECT_NE(normalJson, colourJson);

    EXPECT_EQ(AssetIdFromJson(normalJson), normalMap);
    EXPECT_EQ(AssetIdFromJson(colourJson), colour);
}

TEST(AssetIdSerializeTests, AbsentDescriptorMeansDefaultNotNull)
{
    // A null descriptor hashes differently from a default one, so an id read back without
    // a "desc" key has to end up with the default rather than with nothing.
    JsonValue json = JsonValue::object();
    json["type"] = "Model";
    json["path"] = "test://Asset/CubeTextured.glb";

    const AssetId decoded = AssetIdFromJson(json);
    ASSERT_TRUE(decoded.IsValid());
    EXPECT_NE(decoded.GetDescriptor(), nullptr);
    EXPECT_EQ(decoded, AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb"));
}

TEST(AssetIdSerializeTests, InvalidIdIsNotWritten)
{
    JsonValue json;
    EXPECT_FALSE(AssetIdToJson(AssetId{}, json));
}

TEST(AssetIdSerializeTests, MalformedJsonYieldsInvalidId)
{
    JsonValue missingType = JsonValue::object();
    missingType["path"] = "test://Asset/CubeTextured.glb";
    EXPECT_FALSE(AssetIdFromJson(missingType).IsValid());

    JsonValue missingPath = JsonValue::object();
    missingPath["type"] = "Model";
    EXPECT_FALSE(AssetIdFromJson(missingPath).IsValid());

    JsonValue unknownType = JsonValue::object();
    unknownType["type"] = "Material";   // not an AssetType this build knows
    unknownType["path"] = "test://Asset/Wood.smat";
    EXPECT_FALSE(AssetIdFromJson(unknownType).IsValid());

    EXPECT_FALSE(AssetIdFromJson(JsonValue("just a string")).IsValid());
}

TEST(AssetIdSerializeTests, DisplayString)
{
    EXPECT_EQ(AssetIdToDisplayString(AssetId{}), "None");

    EXPECT_EQ(AssetIdToDisplayString(AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb")),
              "test://Asset/CubeTextured.glb");

    const AssetId sub = AssetId::OfSub<ImageAsset>("test://Asset/CubeTextured.glb", "image/3");
    EXPECT_EQ(AssetIdToDisplayString(sub), "test://Asset/CubeTextured.glb:image/3");
}
