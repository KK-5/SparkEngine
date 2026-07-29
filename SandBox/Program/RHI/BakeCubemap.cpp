// BakeCubemap — standalone verification of the EnvironmentCubemap asset pipeline.
//
// Headless (no window / swapchain): init the DX12 RHI device + AssetManager, then
// LoadAsset<ImageAsset> an equirectangular HDRI with an EnvironmentCubemap descriptor.
// This drives the real path — ImageAssetBuilder::Compile routes through the GPU
// EnvironmentBaker and returns a 6-face cube ImageAssetData (arrayLayers == 6).
//
// Checks three things, in increasing order of how hard they are to catch elsewhere:
//
//  1. SHAPE (automatic, fails the run) — arrayLayers, faceSize, mip count and the exact
//     total byte count of the full mip chain. If the chain never reached the asset layer
//     (e.g. m_mipLevels still hardcoded to 1) the byte total is ~1/1.33 of expected and
//     this exits non-zero.
//
//  2. MIP CONTENT (visual) — each face is dumped as a vertical mip strip: mip 0 on top,
//     then each successive level below it, left-aligned. A correct chain reads as a
//     smooth progressive blur. Uninitialised video memory shows up as noise/garbage in
//     the lower levels, which is exactly what a missing UAV barrier between downsample
//     dispatches produces.
//
//  3. BYTE ORDER (visual, and the whole reason this tool matters) — the dump walks the
//     buffer FACE-MAJOR, MIP-INNER, the contract AsyncUploadSystem relies on. If the
//     baker ever emits mip-major instead, nothing crashes and no assert fires; the strips
//     just come out scrambled, with each "face" made of unrelated levels. That failure
//     mode is otherwise invisible until it reaches the screen as a subtly wrong sky.

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

    uint32_t FaceSizeAtMip(uint32_t faceSize, uint32_t mip)
    {
        const uint32_t s = faceSize >> mip;
        return s > 0 ? s : 1u;
    }

    // Full chain down to 1x1 — mirrors ImageAsset::ComputeMipLevels(w, h, 0), which is
    // what EnvironmentBaker sizes the cube with.
    uint32_t ExpectedMipLevels(uint32_t faceSize)
    {
        uint32_t levels = 1;
        while (faceSize > 1)
        {
            faceSize >>= 1;
            ++levels;
        }
        return levels;
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

    // --- Drive the real pipeline via the registration convention ---
    // MakeAssetId gives .hdr files the EnvironmentCubemap descriptor (see
    // MakeAssetIdForType), so the identity is the baked cube — no explicit descriptor
    // here. Go through the AssetManager interface: the concrete override
    // LoadAsset(AssetId, AssetType) would otherwise name-hide the base LoadAsset<T>.
    const uint32_t faceSize = 1024; // matches DefaultHDRDescriptor's cubemapFaceSize
    auto* am = Service<Resource::AssetManager>::Get();
    Resource::AssetId cubeId = am->MakeAssetId("Image/Table_Defringed_4k2k.hdr");

    const auto* convDesc = static_cast<const Resource::ImageAssetDescriptor*>(cubeId.GetDescriptor());
    if (!convDesc || convDesc->usage != Resource::ImageUsage::EnvironmentCubemap)
    {
        LOG_ERROR("[BakeCubemap] .hdr did not get the EnvironmentCubemap descriptor at registration.");
        return 1;
    }

    Ptr<Resource::ImageAsset> image = am->LoadAsset<Resource::ImageAsset>(cubeId);

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

    // --- Verify the compiled shape, including the full mip chain ---
    // Every subresource is stored tightly (no row padding) and all six faces carry the
    // same chain, so the total is just the per-face geometric series times six.
    const uint32_t expectedMips = ExpectedMipLevels(faceSize);

    size_t perFaceBytes = 0;
    for (uint32_t mip = 0; mip < expectedMips; ++mip)
    {
        const size_t s = FaceSizeAtMip(faceSize, mip);
        perFaceBytes += s * s * 8; // RGBA16F
    }
    const size_t expectedBytes = perFaceBytes * 6;

    LOG_INFO("[BakeCubemap] compiled cube: {}x{}, arrayLayers={}, mips={} (expect {}), "
             "format={}, bytes={} (expect {})",
             data->GetWidth(), data->GetHeight(), data->GetArrayLayers(),
             data->GetMipLevels(), expectedMips,
             static_cast<int>(data->GetFormat()), data->GetTextureBytes().size(), expectedBytes);

    if (data->GetArrayLayers() != 6 || data->GetWidth() != faceSize)
    {
        LOG_ERROR("[BakeCubemap] Unexpected compiled cube shape.");
        return 1;
    }
    if (data->GetMipLevels() != expectedMips)
    {
        LOG_ERROR("[BakeCubemap] Mip count is {}, expected {}. A count of 1 means the chain "
                  "never reached the asset layer (check AssembleCubemapData).",
                  data->GetMipLevels(), expectedMips);
        return 1;
    }
    if (data->GetTextureBytes().size() != expectedBytes)
    {
        LOG_ERROR("[BakeCubemap] Payload is {}B, expected {}B — the mip chain is incomplete "
                  "or the byte layout is not face-major/mip-inner.",
                  data->GetTextureBytes().size(), expectedBytes);
        return 1;
    }

    // --- Dump each face as a vertical mip strip (tonemapped PNG) ---
    // Walk order is FACE-MAJOR, MIP-INNER — the same order AsyncUploadSystem consumes the
    // buffer in. Reading it back the same way is what makes a mip-major regression visible.
    static const char* kFaceNames[6] = { "0_posX", "1_negX", "2_posY", "3_negY", "4_posZ", "5_negZ" };
    const uint8_t* cubeBytes = data->GetTextureBytes().data();

    uint32_t stripHeight = 0;
    for (uint32_t mip = 0; mip < expectedMips; ++mip)
    {
        stripHeight += FaceSizeAtMip(faceSize, mip);
    }

    // Strip canvas: mip 0 on top at full width, each level below left-aligned. The unused
    // right-hand region stays at the memset value, giving the chain a visible staircase.
    eastl::vector<uint8_t> strip(static_cast<size_t>(faceSize) * stripHeight * 4);

    size_t srcOffset = 0;
    for (uint32_t f = 0; f < 6; ++f)
    {
        memset(strip.data(), 0, strip.size());

        uint32_t dstY = 0;
        for (uint32_t mip = 0; mip < expectedMips; ++mip)
        {
            const uint32_t s = FaceSizeAtMip(faceSize, mip);
            const auto* half = reinterpret_cast<const uint16_t*>(cubeBytes + srcOffset);

            for (uint32_t y = 0; y < s; ++y)
            {
                for (uint32_t x = 0; x < s; ++x)
                {
                    const size_t si = (static_cast<size_t>(y) * s + x) * 4;
                    const size_t di = (static_cast<size_t>(dstY + y) * faceSize + x) * 4;
                    strip[di + 0] = ToneMapToByte(HalfToFloat(half[si + 0]));
                    strip[di + 1] = ToneMapToByte(HalfToFloat(half[si + 1]));
                    strip[di + 2] = ToneMapToByte(HalfToFloat(half[si + 2]));
                    strip[di + 3] = 255;
                }
            }

            srcOffset += static_cast<size_t>(s) * s * 8;
            dstY      += s;
        }

        char path[128];
        snprintf(path, sizeof(path), "bake_face_%s_mips.png", kFaceNames[f]);
        if (stbi_write_png(path, faceSize, stripHeight, 4, strip.data(), faceSize * 4))
        {
            LOG_INFO("[BakeCubemap] wrote {} ({}x{}, {} levels)", path, faceSize, stripHeight, expectedMips);
        }
        else
        {
            LOG_ERROR("[BakeCubemap] failed to write {}", path);
        }
    }

    LOG_INFO("[BakeCubemap] Done. Each strip should read as a progressive blur top to "
             "bottom; noise in the lower levels means a missing UAV barrier.");
    return 0;
}
