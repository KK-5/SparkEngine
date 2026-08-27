#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Reflection/TypeRegistry.h>
#include <Serialization/JsonSerializer.h>

#include <Material/Components.h>
#include <Resource/Image/ImageAsset.h>

using namespace Spark;
using namespace Spark::Material;

namespace
{
    constexpr size_t kBaseColorSlot = static_cast<size_t>(MaterialTexSlot::BaseColor);

    //! MaterialParams is the widest real component available here: plain scalars, a Vector4,
    //! an AssetId (which goes through its JsonOperation) and the only Data<Set,Get> fields
    //! in the repo. The mechanism itself is covered by JsonSerializerTest's local types --
    //! this is the check that it holds on an actual component.
    MaterialParams MakeParams()
    {
        MaterialParams params;
        params.m_baseColor        = {0.2f, 0.4f, 0.6f, 1.0f};
        params.m_metallic         = 0.75f;
        params.m_roughness        = 0.25f;
        params.m_emissiveStrength = 3.5f;
        params.m_textures[kBaseColorSlot].m_assetId =
            Resource::AssetId::Of<Resource::ImageAsset>("test://Texture/Wood.png");
        return params;
    }

    MetaType ParamsType() { return TypeRegistry::GetContext().Resolve<MaterialParams>(); }
}

TEST(MaterialSerializeTest, ParamsRoundTrip)
{
    const MaterialParams params = MakeParams();
    const MetaType       type   = ParamsType();
    ASSERT_TRUE(type);

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(type.from_void(&params), json));

    MaterialParams decoded;
    MetaAny        target = type.from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));

    EXPECT_FLOAT_EQ(decoded.m_baseColor.x, 0.2f);
    EXPECT_FLOAT_EQ(decoded.m_baseColor.w, 1.0f);
    EXPECT_FLOAT_EQ(decoded.m_metallic, 0.75f);
    EXPECT_FLOAT_EQ(decoded.m_roughness, 0.25f);
    EXPECT_FLOAT_EQ(decoded.m_emissiveStrength, 3.5f);

    // The slot is written through the reflected setter, the same path the editor's
    // drag-drop uses.
    EXPECT_EQ(decoded.m_textures[kBaseColorSlot].m_assetId,
              params.m_textures[kBaseColorSlot].m_assetId);
    EXPECT_EQ(decoded.m_textures[kBaseColorSlot].m_assetId.GetHash(),
              params.m_textures[kBaseColorSlot].m_assetId.GetHash());
}

TEST(MaterialSerializeTest, ShapeOnDisk)
{
    const MaterialParams params = MakeParams();

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(ParamsType().from_void(&params), json));

    // Every field is present, defaults included -- Specular was never touched.
    EXPECT_TRUE(json.contains("Specular"));

    // Math types are walked field by field, not handed to a codec.
    EXPECT_FLOAT_EQ(json["Base Color"]["x"].get<float>(), 0.2f);
    EXPECT_FLOAT_EQ(json["Base Color"]["w"].get<float>(), 1.0f);

    // An assigned slot is a composite AssetId; an unassigned one is null rather than an
    // error, which is what keeps a default material from failing to save.
    EXPECT_EQ(json["Base Color Map"]["type"], "Image");
    EXPECT_EQ(json["Base Color Map"]["path"], "test://Texture/Wood.png");
    EXPECT_TRUE(json["Normal Map"].is_null());
    EXPECT_TRUE(json["Emissive Map"].is_null());
}

TEST(MaterialSerializeTest, DefaultParamsSurviveARoundTrip)
{
    // The all-empty case: five null slots and nothing but defaults. Encoding it must not
    // fail, and it must come back unchanged.
    const MaterialParams params;
    const MetaType       type = ParamsType();

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(type.from_void(&params), json));

    MaterialParams decoded = MakeParams();
    MetaAny        target  = type.from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));

    EXPECT_FLOAT_EQ(decoded.m_roughness, params.m_roughness);
    EXPECT_FLOAT_EQ(decoded.m_baseColor.x, params.m_baseColor.x);
    EXPECT_FALSE(decoded.m_textures[kBaseColorSlot].m_assetId.IsValid());
}
