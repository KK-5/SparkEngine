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
    constexpr size_t kBaseColorSlot = static_cast<size_t>(Resource::MaterialTexSlot::BaseColor);

    //! Resource::StandardPBR is the widest real component available here: plain scalars, a Vector4,
    //! an AssetId (which goes through its JsonOperation) and the only Data<Set,Get> fields
    //! in the repo. The mechanism itself is covered by JsonSerializerTest's local types --
    //! this is the check that it holds on an actual component.
    Resource::StandardPBR MakeParams()
    {
        Resource::StandardPBR params;
        params.m_baseColor        = {0.2f, 0.4f, 0.6f, 1.0f};
        params.m_metallic         = 0.75f;
        params.m_roughness        = 0.25f;
        params.m_emissiveStrength = 3.5f;
        params.m_textures[kBaseColorSlot] =
            Resource::AssetId::Of<Resource::ImageAsset>("test://Texture/Wood.png");
        return params;
    }

    MetaType ParamsType() { return TypeRegistry::GetContext().Resolve<Resource::StandardPBR>(); }
}

TEST(MaterialSerializeTest, ParamsRoundTrip)
{
    const Resource::StandardPBR params = MakeParams();
    const MetaType    type   = ParamsType();
    ASSERT_TRUE(type);

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(type.from_void(&params), json));

    Resource::StandardPBR decoded;
    MetaAny     target = type.from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));

    EXPECT_FLOAT_EQ(decoded.m_baseColor.r, 0.2f);
    EXPECT_FLOAT_EQ(decoded.m_baseColor.a, 1.0f);
    EXPECT_FLOAT_EQ(decoded.m_metallic, 0.75f);
    EXPECT_FLOAT_EQ(decoded.m_roughness, 0.25f);
    EXPECT_FLOAT_EQ(decoded.m_emissiveStrength, 3.5f);

    // The slot is written through the reflected setter, the same path the editor's
    // drag-drop uses.
    EXPECT_EQ(decoded.m_textures[kBaseColorSlot],
              params.m_textures[kBaseColorSlot]);
    EXPECT_EQ(decoded.m_textures[kBaseColorSlot].GetHash(),
              params.m_textures[kBaseColorSlot].GetHash());
}

TEST(MaterialSerializeTest, ShapeOnDisk)
{
    const Resource::StandardPBR params = MakeParams();

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(ParamsType().from_void(&params), json));

    // Every field is present, defaults included -- Specular was never touched.
    EXPECT_TRUE(json.contains("Specular"));

    // The colour factor spells Color out: it sits next to Emissive Strength, and a bare
    // "Emissive" beside "Emissive Map" reads as that map's on/off switch.
    EXPECT_TRUE(json.contains("Emissive Color"));
    EXPECT_FALSE(json.contains("Emissive"));

    // Math types are walked field by field, not handed to a codec -- and a colour is
    // its own type, so it spells itself r/g/b/a rather than borrowing Vector4's names.
    EXPECT_FLOAT_EQ(json["Base Color"]["r"].get<float>(), 0.2f);
    EXPECT_FLOAT_EQ(json["Base Color"]["a"].get<float>(), 1.0f);

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
    const Resource::StandardPBR params;
    const MetaType    type = ParamsType();

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(type.from_void(&params), json));

    Resource::StandardPBR decoded = MakeParams();
    MetaAny     target  = type.from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));

    EXPECT_FLOAT_EQ(decoded.m_roughness, params.m_roughness);
    EXPECT_FLOAT_EQ(decoded.m_baseColor.r, params.m_baseColor.r);
    EXPECT_FALSE(decoded.m_textures[kBaseColorSlot].IsValid());
}
