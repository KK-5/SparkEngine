#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Resource/AssetJsonSerializer.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Shader/ShaderAsset.h>

using namespace Spark;
using namespace Spark::Resource;

namespace
{
    //! Round-trip judged by re-encoding, not by Hash(): ShaderDescriptor::Hash() folds in
    //! `backend` alone and ImageAssetDescriptor::Hash() skips cubemapFaceSize outside the
    //! cubemap usages, so a hash comparison would pass with fields dropped on the floor.
    JsonValue RoundTrip(const AssetDescriptor& descriptor, AssetType type)
    {
        JsonValue encoded;
        EXPECT_TRUE(DescriptorToJson(descriptor, type, encoded));

        Ptr<AssetDescriptor> decoded = DescriptorFromJson(type, encoded);
        EXPECT_TRUE(decoded);

        JsonValue reEncoded;
        EXPECT_TRUE(DescriptorToJson(*decoded, type, reEncoded));
        EXPECT_EQ(encoded, reEncoded);

        return encoded;
    }
}

TEST(DescriptorSerializeTests, DefaultDescriptorsEncodeToEmptyObject)
{
    const ImageAssetDescriptor image;
    const ModelAssetDescriptor model;
    const ShaderDescriptor     shader;

    JsonValue encoded;
    ASSERT_TRUE(DescriptorToJson(image, AssetType::Image, encoded));
    EXPECT_EQ(encoded.dump(), "{}");

    ASSERT_TRUE(DescriptorToJson(model, AssetType::Model, encoded));
    EXPECT_EQ(encoded.dump(), "{}");

    ASSERT_TRUE(DescriptorToJson(shader, AssetType::Shader, encoded));
    EXPECT_EQ(encoded.dump(), "{}");
}

TEST(DescriptorSerializeTests, ImageDescriptorRoundTrips)
{
    ImageAssetDescriptor image;
    image.compression     = TextureCompression::BC5_RG;
    image.colorSpace      = ImageColorSpace::Linear;
    image.maxMipLevels    = 4;
    image.usage           = ImageUsage::NormalMap;
    image.cubemapFaceSize = 512;

    const JsonValue encoded = RoundTrip(image, AssetType::Image);
    EXPECT_EQ(encoded.dump(),
        R"({"compression":"BC5_RG","colorSpace":"Linear","maxMipLevels":4,"usage":"NormalMap","cubemapFaceSize":512})");

    Ptr<AssetDescriptor> decoded = DescriptorFromJson(AssetType::Image, encoded);
    const auto& typed = static_cast<const ImageAssetDescriptor&>(*decoded);
    EXPECT_EQ(typed.compression, TextureCompression::BC5_RG);
    EXPECT_EQ(typed.colorSpace, ImageColorSpace::Linear);
    EXPECT_EQ(typed.maxMipLevels, 4u);
    EXPECT_EQ(typed.usage, ImageUsage::NormalMap);
    // Hash() ignores this one for a 2D usage, which is exactly why the round-trip judge
    // cannot be Hash equality.
    EXPECT_EQ(typed.cubemapFaceSize, 512u);
}

TEST(DescriptorSerializeTests, ModelDescriptorRoundTrips)
{
    ModelAssetDescriptor model;
    model.type = ModelAssetType::Unknown;   // GLTF is the default, so Unknown is the change

    const JsonValue encoded = RoundTrip(model, AssetType::Model);
    EXPECT_EQ(encoded.dump(), R"({"type":"Unknown"})");

    Ptr<AssetDescriptor> decoded = DescriptorFromJson(AssetType::Model, encoded);
    EXPECT_EQ(static_cast<const ModelAssetDescriptor&>(*decoded).type, ModelAssetType::Unknown);
}

TEST(DescriptorSerializeTests, ShaderStagesRoundTrip)
{
    ShaderDescriptor shader;
    shader.backend = ShaderBackend::SPIRV;
    shader.stages.push_back(ShaderStageEntry{RHI::ShaderStage::Vertex, "VSMain", "vs_6_0"});
    shader.stages.push_back(ShaderStageEntry{RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"});

    const JsonValue encoded = RoundTrip(shader, AssetType::Shader);
    EXPECT_EQ(encoded.dump(),
        R"({"backend":"SPIRV","stages":[{"stage":"Vertex","entryPoint":"VSMain","targetProfile":"vs_6_0"},)"
        R"({"stage":"Fragment","entryPoint":"PSMain","targetProfile":"ps_6_0"}]})");

    Ptr<AssetDescriptor> decoded = DescriptorFromJson(AssetType::Shader, encoded);
    const auto& typed = static_cast<const ShaderDescriptor&>(*decoded);
    EXPECT_EQ(typed.backend, ShaderBackend::SPIRV);
    ASSERT_EQ(typed.stages.size(), 2u);
    EXPECT_EQ(typed.stages[0].stage, RHI::ShaderStage::Vertex);
    EXPECT_EQ(typed.stages[0].entryPoint, "VSMain");
    EXPECT_EQ(typed.stages[1].targetProfile, "ps_6_0");

    // stages is absent from Hash(), so the identity hash cannot speak for it either way.
    EXPECT_EQ(typed.Hash(), shader.Hash());
}

TEST(DescriptorSerializeTests, DecodedDescriptorIsNotTheSharedSingleton)
{
    // Filling ImageAsset::DefaultDescriptor() in place would rewrite the descriptor that
    // every existing image AssetId shares.
    Ptr<AssetDescriptor> shared = ImageAsset::DefaultDescriptor();
    Ptr<AssetDescriptor> first  = DescriptorFromJson(AssetType::Image, JsonValue::object());
    Ptr<AssetDescriptor> second = DescriptorFromJson(AssetType::Image, JsonValue::object());

    EXPECT_NE(first.get(), shared.get());
    EXPECT_NE(first.get(), second.get());

    const auto& sharedImage = static_cast<const ImageAssetDescriptor&>(*shared);
    EXPECT_EQ(sharedImage.usage, ImageUsage::Texture2D);
}

TEST(DescriptorSerializeTests, MissingKeysKeepDefaults)
{
    JsonValue partial = JsonValue::object();
    partial["usage"] = "EnvironmentCubemap";

    Ptr<AssetDescriptor> decoded = DescriptorFromJson(AssetType::Image, partial);
    ASSERT_TRUE(decoded);
    const auto& typed = static_cast<const ImageAssetDescriptor&>(*decoded);
    EXPECT_EQ(typed.usage, ImageUsage::EnvironmentCubemap);
    EXPECT_EQ(typed.compression, TextureCompression::BC3_RGBA);
    EXPECT_EQ(typed.colorSpace, ImageColorSpace::sRGB);
}

TEST(DescriptorSerializeTests, UnknownAssetTypeYieldsNothing)
{
    EXPECT_EQ(DescriptorFromJson(AssetType::Unknown, JsonValue::object()), nullptr);

    const ImageAssetDescriptor image;
    JsonValue encoded;
    EXPECT_FALSE(DescriptorToJson(image, AssetType::Unknown, encoded));
}
