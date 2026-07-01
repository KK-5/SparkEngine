// BakeCubemap — standalone verification of the EnvironmentCubemap asset pipeline.
//
// Headless (no window / swapchain): init the DX12 RHI device + AssetManager, then
// LoadAsset<ImageAsset> an equirectangular HDRI with an EnvironmentCubemap descriptor.
// This drives the real path — ImageAssetBuilder::Compile routes through the GPU
// EnvironmentBaker and returns a 6-face cube ImageAssetData (arrayLayers == 6). We
// verify the shape, then dump the six faces as PNG (RGBA16F -> Reinhard tonemap + gamma)
// to eyeball direction correctness + seam continuity.

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Device/Device.h>
#include <RHI/Backend/DX12/RHISystem.h>

#include <Resource/AssetManager.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <EASTL/vector.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace Spark;

namespace
{
    // IEEE 754 binary16 -> binary32.
    float HalfToFloat(uint16_t h)
    {
        const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
        uint32_t       exp  = (h >> 10) & 0x1Fu;
        uint32_t       mant = h & 0x3FFu;
        uint32_t       bits;

        if (exp == 0)
        {
            if (mant == 0)
            {
                bits = sign; // +/- 0
            }
            else
            {
                exp = 1;
                while ((mant & 0x400u) == 0) { mant <<= 1; --exp; } // normalize
                mant &= 0x3FFu;
                bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
            }
        }
        else if (exp == 0x1Fu)
        {
            bits = sign | 0x7F800000u | (mant << 13); // inf / nan
        }
        else
        {
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }

        float out;
        memcpy(&out, &bits, sizeof(out));
        return out;
    }

    uint8_t ToneMapToByte(float linear)
    {
        const float mapped = linear / (linear + 1.0f);      // Reinhard
        const float gamma  = powf(mapped, 1.0f / 2.2f);     // to sRGB-ish
        const int   v      = static_cast<int>(gamma * 255.0f + 0.5f);
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
}

int main(int, char**)
{
    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem> logger = eastl::make_unique<SpdLogSystem>(logConfig);

    // --- RHI device (headless) ---
    auto rhiSystem = CreateSystem<RHI::DX12::RHISystem>();
    rhiSystem->Init();
    auto* rhi = Service<RHI::RHIInterface>::Get();
    RHI::PhysicalDeviceList devices = rhi->EnumeratePhysicalDevices();
    if (devices.empty())
    {
        LOG_ERROR("[BakeCubemap] No physical devices.");
        return 1;
    }
    RHI::DeviceDescriptor devDesc;
    devDesc.m_frameCountMax = 2;
    if (rhi->InitDevice(*devices[0], devDesc) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[BakeCubemap] InitDevice failed.");
        return 1;
    }

    // --- AssetManager: search paths + baker up before requesting the cube asset ---
    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();
    assetManager->AddSearchPath(ENGINE_ASSET_DIR); // "Shaders/Image/EnvironmentBake.hlsl"
    assetManager->AddSearchPath(SHADER_ASSET_DIR); // SandBox/Asset (the HDRI)
    if (!assetManager->InitEnvironmentBaker())
    {
        LOG_ERROR("[BakeCubemap] InitEnvironmentBaker failed.");
        return 1;
    }

    // --- Drive the real pipeline: an EnvironmentCubemap image asset ---
    // usage == EnvironmentCubemap routes Compile through the GPU baker; Linear + None
    // keeps the HDR float data intact. Descriptor is part of the AssetId hash, so this
    // is a distinct asset from the same .hdr loaded as a plain 2D texture.
    const uint32_t faceSize = 1024;
    Resource::ImageAssetDescriptor cubeDesc;
    cubeDesc.usage           = Resource::ImageUsage::EnvironmentCubemap;
    cubeDesc.colorSpace      = Resource::ImageColorSpace::Linear;
    cubeDesc.compression     = Resource::TextureCompression::None;
    cubeDesc.cubemapFaceSize = faceSize;

    // Go through the AssetManager interface: the concrete override
    // LoadAsset(AssetId, AssetType) would otherwise name-hide the base LoadAsset<T>.
    auto* am = Service<Resource::AssetManager>::Get();
    Ptr<Resource::ImageAsset> image = am->LoadAsset<Resource::ImageAsset>(
        Resource::AssetId::Of<Resource::ImageAsset>("Image/Table_Defringed_4k2k.hdr", cubeDesc));

    if (!image || image->GetStatus() != Resource::AssetStatus::Ready)
    {
        LOG_ERROR("[BakeCubemap] Cubemap asset failed to load/compile (status={}).",
                  image ? static_cast<int>(image->GetStatus()) : -1);
        return 1;
    }

    const Resource::ImageAssetData* data = image->GetImageData();
    if (!data)
    {
        LOG_ERROR("[BakeCubemap] Cubemap asset has no image data.");
        return 1;
    }

    // --- Verify the compiled shape ---
    const size_t faceTexels     = static_cast<size_t>(faceSize) * faceSize;
    const size_t faceByteStride = faceTexels * 8; // RGBA16F, tight
    const size_t expectedBytes  = faceByteStride * 6;
    LOG_INFO("[BakeCubemap] compiled cube: {}x{}, arrayLayers={}, mips={}, format={}, bytes={} (expect {})",
             data->GetWidth(), data->GetHeight(), data->GetArrayLayers(), data->GetMipLevels(),
             static_cast<int>(data->GetFormat()), data->GetTextureBytes().size(), expectedBytes);
    if (data->GetArrayLayers() != 6 || data->GetWidth() != faceSize ||
        data->GetTextureBytes().size() != expectedBytes)
    {
        LOG_ERROR("[BakeCubemap] Unexpected compiled cube shape.");
        return 1;
    }

    // --- Dump faces as tonemapped PNG (face-major, RGBA16F) ---
    static const char* kFaceNames[6] = { "0_posX", "1_negX", "2_posY", "3_negY", "4_posZ", "5_negZ" };
    const uint8_t* faceBytes = data->GetTextureBytes().data();

    eastl::vector<uint8_t> rgba8(faceTexels * 4);
    for (uint32_t f = 0; f < 6; ++f)
    {
        const auto* faceHalf = reinterpret_cast<const uint16_t*>(faceBytes + f * faceByteStride);
        for (size_t i = 0; i < faceTexels; ++i)
        {
            rgba8[i * 4 + 0] = ToneMapToByte(HalfToFloat(faceHalf[i * 4 + 0]));
            rgba8[i * 4 + 1] = ToneMapToByte(HalfToFloat(faceHalf[i * 4 + 1]));
            rgba8[i * 4 + 2] = ToneMapToByte(HalfToFloat(faceHalf[i * 4 + 2]));
            rgba8[i * 4 + 3] = 255;
        }
        char path[128];
        snprintf(path, sizeof(path), "bake_face_%s.png", kFaceNames[f]);
        if (stbi_write_png(path, faceSize, faceSize, 4, rgba8.data(), faceSize * 4))
        {
            LOG_INFO("[BakeCubemap] wrote {}", path);
        }
        else
        {
            LOG_ERROR("[BakeCubemap] failed to write {}", path);
        }
    }

    LOG_INFO("[BakeCubemap] Done.");
    return 0;
}
