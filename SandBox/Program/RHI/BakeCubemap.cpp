// BakeCubemap — standalone runtime verification of EnvironmentBaker.
//
// Headless (no window / swapchain): init the DX12 RHI device + AssetManager, load
// an equirectangular HDRI through the engine's ImageAssetLoader, run the compute
// bake, and dump the six readback faces as PNG (RGBA16F -> Reinhard tonemap + gamma).
// Eyeball the PNGs for direction correctness + seam continuity.

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
#include <Resource/Image/ImageAssetLoader.h>
#include <Resource/Image/EnvironmentBaker.h>

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

    // --- AssetManager (the baker resolves its bake shader through this) ---
    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();
    assetManager->AddSearchPath(ENGINE_ASSET_DIR); // "Shaders/Image/EnvironmentBake.hlsl"
    assetManager->AddSearchPath(SHADER_ASSET_DIR); // SandBox/Asset

    // --- Load equirect HDRI through the engine loader (no raw stbi) ---
    Resource::ImageAssetLoader loader;
    loader.SetSearchPaths({ eastl::string(SHADER_ASSET_DIR) });
    LOG_INFO("[BakeCubemap] loader search path: {}", SHADER_ASSET_DIR);
    UniquePtr<Resource::AssetData> raw =
        loader.Load(Resource::AssetId::Of<Resource::ImageAsset>("Image/Table_Defringed_4k2k.hdr"));
    LOG_INFO("[BakeCubemap] loader.Load returned: {}", raw ? "non-null" : "NULL");
    auto* equirect = static_cast<Resource::ImageAssetRawData*>(raw.get());
    if (equirect)
    {
        LOG_INFO("[BakeCubemap] raw: {}x{}, pixelBytes={}",
                 equirect->GetWidth(), equirect->GetHeight(), equirect->GetPixels().size());
    }
    if (!equirect || equirect->GetPixels().empty())
    {
        LOG_ERROR("[BakeCubemap] Failed to load equirect HDRI.");
        return 1;
    }
    LOG_INFO("[BakeCubemap] Loaded equirect {}x{}, HDR={}",
             equirect->GetWidth(), equirect->GetHeight(), equirect->IsHDR());

    // --- Bake ---
    Resource::EnvironmentBaker baker;
    if (!baker.Init())
    {
        LOG_ERROR("[BakeCubemap] Baker init failed.");
        return 1;
    }
    LOG_INFO("[BakeCubemap] baker.Init OK -> baking...");
    const uint32_t faceSize = 1024;
    Resource::BakedCubemap cube = baker.Bake(*equirect, faceSize);
    LOG_INFO("[BakeCubemap] Bake returned (valid={}).", cube.IsValid());
    if (!cube.IsValid())
    {
        LOG_ERROR("[BakeCubemap] Bake produced no result.");
        return 1;
    }
    LOG_INFO("[BakeCubemap] Baked cube: {} faces @ {}px.", 6, cube.faceSize);

    // --- Dump faces as tonemapped PNG ---
    static const char* kFaceNames[6] = { "0_posX", "1_negX", "2_posY", "3_negY", "4_posZ", "5_negZ" };
    const size_t faceTexels    = static_cast<size_t>(faceSize) * faceSize;
    const size_t faceByteStride = faceTexels * 8; // RGBA16F

    eastl::vector<uint8_t> rgba8(faceTexels * 4);
    for (uint32_t f = 0; f < 6; ++f)
    {
        const auto* faceHalf = reinterpret_cast<const uint16_t*>(cube.faceBytes.data() + f * faceByteStride);
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
