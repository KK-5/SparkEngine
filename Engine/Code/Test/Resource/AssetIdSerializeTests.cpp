#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Reflection/TypeRegistry.h>
#include <Serialization/JsonSerializer.h>
#include <Serialization/MetaFieldTraits.h>

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

    // `sub` is absent because this is a top-level asset, not because it equals a default.
    // `desc` is present even though it is a plain default -- nothing is omitted.
    EXPECT_EQ(json.dump(),
        R"({"type":"Model","path":"test://Asset/CubeTextured.glb","desc":{"type":"GLTF"}})");

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
        R"("desc":{"compression":"BC3_RGBA","colorSpace":"Linear","maxMipLevels":0,)"
        R"("usage":"NormalMap","cubemapFaceSize":0}})");

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

// ---- AssetId as a field, i.e. through its JsonOperation ------------------------------

namespace
{
    //! Stands in for the component fields this exists for: an unassigned model slot, an
    //! empty texture slot.
    struct Slot
    {
        AssetId asset;
    };

    MetaType ReflectSlot()
    {
        TypeRegistry::GetContext().Reflect<Slot>()
            .Type("Slot")
            .Data<&Slot::asset>("asset").Traits(MetaFieldTraits::Serializable);
        return TypeRegistry::GetContext().Resolve<Slot>();
    }
}

TEST(AssetIdOperationTests, UnsetIdIsNullRatherThanAnError)
{
    // An empty slot is the normal case, not a fault: wiring AssetIdToJson straight in
    // would log an error per empty slot and fail the whole component.
    JsonValue json;
    EXPECT_TRUE(AssetIdToJsonField(AssetId{}, json));
    EXPECT_TRUE(json.is_null());

    AssetId target = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb");
    EXPECT_TRUE(AssetIdFromJsonField(JsonValue(), target));
    EXPECT_FALSE(target.IsValid());
}

TEST(AssetIdOperationTests, BrokenJsonIsDistinguishedFromUnset)
{
    // AssetIdFromJson hands back a default id on failure too, so only this layer can tell
    // "nothing was assigned" from "this file is damaged".
    AssetId target;
    JsonValue missingType;
    missingType["path"] = "test://Asset/CubeTextured.glb";
    EXPECT_FALSE(AssetIdFromJsonField(missingType, target));
}

TEST(AssetIdOperationTests, FieldKeepsItsDescriptor)
{
    // The reason AssetId needs an operation at all. Without one the field falls into the
    // object branch, which reaches no getter and drops `desc` -- and a normal map read
    // back without its descriptor becomes an sRGB colour texture.
    const MetaType slotType = ReflectSlot();

    Slot slot;
    slot.asset = AssetId::Of("test://Asset/CubeTextured.glb", "image/3",
                             AssetType::Image, NormalMapDescriptor());

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(slotType.from_void(&slot), json));
    EXPECT_EQ(json.dump(),
        R"({"asset":{"type":"Image","path":"test://Asset/CubeTextured.glb","sub":"image/3",)"
        R"("desc":{"compression":"BC3_RGBA","colorSpace":"Linear","maxMipLevels":0,)"
        R"("usage":"NormalMap","cubemapFaceSize":0}}})");

    Slot decoded;
    MetaAny target = slotType.from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));
    EXPECT_EQ(decoded.asset, slot.asset);
    EXPECT_EQ(decoded.asset.GetHash(), slot.asset.GetHash());
}
