#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <Resource/AssetTypes.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Shader/ShaderAsset.h>

using namespace Spark;
using namespace Spark::Resource;

namespace
{
    // Every descriptor configuration the engine builds ids from today.
    eastl::vector<Ptr<AssetDescriptor>> AllCanonicalDescriptors()
    {
        eastl::vector<Ptr<AssetDescriptor>> all;
        for (uint8_t u = 0; u <= static_cast<uint8_t>(ImageUsage::PrefilteredCubemap); ++u)
        {
            all.push_back(ImageAsset::DescriptorForUsage(static_cast<ImageUsage>(u)));
        }
        all.push_back(ModelAsset::DefaultDescriptor());
        all.push_back(ShaderAsset::DefaultDescriptor());
        return all;
    }
}

// The descriptor hash is the one part of an AssetId that operator== still leaves to the
// hash, so descriptors that hash alike are indistinguishable. Before the type seed was
// folded in, ModelAssetType::GLTF and ShaderBackend::SPIRV both hashed their bare value
// of 1 and collided.
TEST(DescriptorHashTest, DescriptorsOfDifferentTypesDoNotCollide)
{
    const auto all = AllCanonicalDescriptors();
    for (size_t i = 0; i < all.size(); ++i)
    {
        for (size_t j = i + 1; j < all.size(); ++j)
        {
            EXPECT_NE(all[i]->Hash(), all[j]->Hash()) << "descriptor " << i << " vs " << j;
        }
    }
}

TEST(DescriptorHashTest, HashCoversEveryField)
{
    ImageAssetDescriptor base;
    const AssetHash baseHash = base.Hash();

    ImageAssetDescriptor mips = base;
    mips.maxMipLevels = 4;
    EXPECT_NE(mips.Hash(), baseHash);

    ImageAssetDescriptor compressed = base;
    compressed.compression = TextureCompression::BC7_RGBA;
    EXPECT_NE(compressed.Hash(), baseHash);

    ImageAssetDescriptor linear = base;
    linear.colorSpace = ImageColorSpace::Linear;
    EXPECT_NE(linear.Hash(), baseHash);
}

TEST(DescriptorHashTest, ModelDescriptorDefaultsToGltf)
{
    const ModelAssetDescriptor desc;
    EXPECT_EQ(desc.type, ModelAssetType::GLTF);
}
