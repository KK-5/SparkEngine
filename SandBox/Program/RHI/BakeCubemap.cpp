// BakeCubemap — standalone verification of the EnvironmentBaker products.
//
// Headless: init RHI + AssetManager, load an equirect HDRI, drive Bake() directly for the
// full BakedEnvironment (bypasses the asset layer, which doesn't yet expose the sub-products).
// Per product: checks shape + byte total (fails the run on mismatch), and dumps each face as
// a vertical mip strip (walked FACE-MAJOR, MIP-INNER) for visual inspection.

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>
#include <Math/MathUtils.h>

#include <RHI/RHIInterface.h>
#include <RHI/Device/Device.h>
#include <RHI/Backend/DX12/RHISystem.h>

#include <Reflection/TypeRegistry.h>

#include <Resource/AssetManager.h>
#include <VFS/VFSSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Reflect.h>
#include <Resource/Cache/AssetCache.h>
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
#include <filesystem>

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
        const float gamma  = Math::Pow(mapped, 1.0f / 2.2f);   // to sRGB-ish
        const int   v      = static_cast<int>(gamma * 255.0f + 0.5f);
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }

    uint32_t FaceSizeAtMip(uint32_t faceSize, uint32_t mip)
    {
        const uint32_t s = faceSize >> mip;
        return s > 0 ? s : 1u;
    }

    // Full chain down to 1x1 — mirrors ImageAsset::ComputeMipLevels(w, h, 0), which is
    // what EnvironmentBaker sizes the sky cube with.
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

    // Shape check: faceSize, mip count, and the exact tight byte total (six faces, no padding).
    bool VerifyCube(const Resource::BakedCubemap& cube, uint32_t expFace, uint32_t expMips,
                    const char* label)
    {
        if (!cube.IsValid())
        {
            LOG_ERROR("[BakeCubemap] {}: invalid (empty) cube.", label);
            return false;
        }

        size_t expBytes = 0;
        for (uint32_t mip = 0; mip < expMips; ++mip)
        {
            const size_t s = FaceSizeAtMip(expFace, mip);
            expBytes += s * s * 8; // RGBA16F
        }
        expBytes *= 6;

        LOG_INFO("[BakeCubemap] {}: {}x{}, mips={} (expect {}), bytes={} (expect {})",
                 label, cube.faceSize, cube.faceSize, cube.mipLevels, expMips,
                 cube.faceBytes.size(), expBytes);

        if (cube.faceSize != expFace || cube.mipLevels != expMips ||
            cube.faceBytes.size() != expBytes)
        {
            LOG_ERROR("[BakeCubemap] {}: shape mismatch — the chain is incomplete or the byte "
                      "layout is not face-major/mip-inner.", label);
            return false;
        }
        return true;
    }

    // Dump each face as a vertical mip strip (tonemapped PNG), walked FACE-MAJOR, MIP-INNER
    // so a mip-major regression shows up as scrambled strips.
    void DumpCubeStrips(const Resource::BakedCubemap& cube, const char* prefix)
    {
        static const char* kFaceNames[6] = { "0_posX", "1_negX", "2_posY", "3_negY", "4_posZ", "5_negZ" };
        const uint32_t faceSize  = cube.faceSize;
        const uint32_t mipLevels = cube.mipLevels;
        const uint8_t* bytes     = cube.faceBytes.data();

        uint32_t stripHeight = 0;
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            stripHeight += FaceSizeAtMip(faceSize, mip);
        }

        // mip 0 on top at full width, each level below left-aligned (unused region cleared).
        eastl::vector<uint8_t> strip(static_cast<size_t>(faceSize) * stripHeight * 4);

        size_t srcOffset = 0;
        for (uint32_t f = 0; f < 6; ++f)
        {
            memset(strip.data(), 0, strip.size());

            uint32_t dstY = 0;
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                const uint32_t s = FaceSizeAtMip(faceSize, mip);
                const auto* half = reinterpret_cast<const uint16_t*>(bytes + srcOffset);

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
            snprintf(path, sizeof(path), "%s_face_%s.png", prefix, kFaceNames[f]);
            if (stbi_write_png(path, faceSize, stripHeight, 4, strip.data(), faceSize * 4))
            {
                LOG_INFO("[BakeCubemap] wrote {} ({}x{}, {} levels)", path, faceSize, stripHeight, mipLevels);
            }
            else
            {
                LOG_ERROR("[BakeCubemap] failed to write {}", path);
            }
        }
    }

    //! Drives one HDRI through AssetManager: the sky cube is the top-level asset, and the
    //! two IBL products hang off it as sub-assets, Ready and registered in the db.
    bool VerifyAssetLayer(Resource::SparkAssetManager& assetManager, const Resource::AssetId& hdrId)
    {
        if (!assetManager.InitEnvironmentBaker())
        {
            LOG_ERROR("[BakeCubemap] asset layer: InitEnvironmentBaker failed.");
            return false;
        }

        Ptr<Resource::Asset> asset = assetManager.LoadAsset(hdrId);
        if (!asset || !asset->IsReady())
        {
            LOG_ERROR("[BakeCubemap] asset layer: the sky cube asset is not Ready.");
            return false;
        }

        auto* sky = static_cast<Resource::ImageAsset*>(asset.get());
        const Ptr<Resource::ImageAsset> irr = sky->GetIrradianceAsset();
        const Ptr<Resource::ImageAsset> pre = sky->GetPrefilteredAsset();
        if (!irr || !pre)
        {
            LOG_ERROR("[BakeCubemap] asset layer: the sky cube carries no IBL products.");
            return false;
        }

        // This path takes the auto face size (the descriptor's cubemapFaceSize is 0), so the
        // prefiltered cap is measured against the sky cube the bake actually produced.
        const uint32_t skyFaceSize = static_cast<uint32_t>(sky->GetWidth());

        struct Expected { const char* name; const Resource::ImageAsset* asset;
                          uint32_t size; uint32_t mips; };
        const Expected expected[] = {
            {"irradiance",  irr.get(), Resource::EnvironmentBaker::kIrradianceSize, 1},
            {"prefiltered", pre.get(), Resource::EnvironmentBaker::PrefilterFaceSize(skyFaceSize),
                            Resource::EnvironmentBaker::kPrefilterMips},
        };

        bool ok = true;
        for (const Expected& e : expected)
        {
            const auto* data = e.asset->GetImageData();
            const uint32_t layers = data ? data->GetArrayLayers() : 0;
            const bool     isCube = data && data->IsCubemap();
            LOG_INFO("[BakeCubemap] asset layer: {} = {}x{} x{} mips x{} slices, cube={}, ready={}",
                     e.name, e.asset->GetWidth(), e.asset->GetHeight(),
                     e.asset->GetMipLevels(), layers, isCube, e.asset->IsReady());

            if (!e.asset->IsReady() || static_cast<uint32_t>(e.asset->GetWidth()) != e.size
                || e.asset->GetMipLevels() != e.mips || layers != 6 || !isCube)
            {
                LOG_ERROR("[BakeCubemap] asset layer: {} expected {}^2 x{} mips x6 cube faces.",
                          e.name, e.size, e.mips);
                ok = false;
            }

            // Must be the very instance in the db: otherwise resolving by id and going
            // through the parent would hand out different objects.
            if (assetManager.FindAsset(e.asset->GetAssetId()).get() != e.asset)
            {
                LOG_ERROR("[BakeCubemap] asset layer: {} is not the db-registered instance.",
                          e.name);
                ok = false;
            }
        }
        return ok;
    }

    //! The unit cache end to end, which no gtest can reach: a bake is the only thing that
    //! produces a multi-asset unit, and it needs a GPU. A second run must restore the sky
    //! AND both IBL products from disk, with the baker never touched.
    bool VerifyUnitCache(Spark::VFSSystem& fileSystem, const Resource::AssetId& hdrId,
                         const std::filesystem::path& cacheDir)
    {
        size_t fileCount = 0;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(cacheDir, ec),
                  end = std::filesystem::recursive_directory_iterator();
             it != end; it.increment(ec))
        {
            if (!it->is_directory(ec))
            {
                ++fileCount;
            }
        }

        // sky + irradiance + prefiltered payloads, plus the manifest.
        LOG_INFO("[BakeCubemap] unit cache: {} files written", fileCount);
        if (fileCount != 4)
        {
            LOG_ERROR("[BakeCubemap] unit cache: expected 4 files (3 payloads + manifest).");
            return false;
        }

        // A fresh manager over the same cache, and deliberately NO InitEnvironmentBaker:
        // if anything below reaches the bake path it fails instead of silently recomputing.
        auto second = CreateSystem<Resource::SparkAssetManager>();
        second->Init();

        Ptr<Resource::Asset> asset = second->LoadAsset(hdrId);
        if (!asset || !asset->IsReady())
        {
            LOG_ERROR("[BakeCubemap] unit cache: the sky cube did not come back from cache.");
            return false;
        }

        auto* sky = static_cast<Resource::ImageAsset*>(asset.get());
        const Ptr<Resource::ImageAsset> irr = sky->GetIrradianceAsset();
        const Ptr<Resource::ImageAsset> pre = sky->GetPrefilteredAsset();
        if (!irr || !pre || !irr->IsReady() || !pre->IsReady())
        {
            LOG_ERROR("[BakeCubemap] unit cache: the restored sky is missing its IBL products.");
            return false;
        }

        LOG_INFO("[BakeCubemap] unit cache: restored sky {}^2 x{} mips, irradiance {}^2, "
                 "prefiltered {}^2 x{} -- no bake",
                 sky->GetWidth(), sky->GetMipLevels(), irr->GetWidth(),
                 pre->GetWidth(), pre->GetMipLevels());

        const auto* skyData = sky->GetImageData();
        const auto* irrData = irr->GetImageData();
        if (!skyData || !skyData->IsCubemap() || !irrData || !irrData->IsCubemap())
        {
            LOG_ERROR("[BakeCubemap] unit cache: a restored payload is not a cube.");
            return false;
        }
        return true;
    }
}

int main(int, char**)
{
    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem> logger = eastl::make_unique<SpdLogSystem>(logConfig);

    // AssetId <-> JSON runs off reflection, and the cache's identity is that JSON.
    // Register only queues; RegisterAll is what runs them.
    TypeRegistry::Register(Spark::Resource::Reflect);
    TypeRegistry::RegisterAll();

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

    // --- Mounts so the baker's Init can resolve its compute shaders ---
    auto fileSystem = CreateSystem<VFSSystem>();
    fileSystem->Init();
    fileSystem->Mount("engine", ENGINE_ASSET_DIR);   // "engine://Shaders/Image/*.hlsl"
    fileSystem->Mount("sandbox", SHADER_ASSET_DIR);  // SandBox/Asset (the HDRI)

    // Its own empty cache directory, so the unit-cache check below measures this run.
    const std::filesystem::path cacheDir =
        std::filesystem::temp_directory_path() / "SparkBakeCubemapCache";
    std::error_code cacheEc;
    std::filesystem::remove_all(cacheDir, cacheEc);
    std::filesystem::create_directories(cacheDir, cacheEc);
    fileSystem->Mount(Resource::kCacheMountName,
                      eastl::string(cacheDir.generic_string().c_str()));

    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();

    // --- Standalone baker: own its PSOs / queue / pools, drive Bake() directly ---
    Resource::EnvironmentBaker baker;
    if (!baker.Init())
    {
        LOG_ERROR("[BakeCubemap] EnvironmentBaker Init failed.");
        return 1;
    }

    // --- Load the equirect HDRI as raw pixels (RGBAF32) --- 
    auto* am = Service<Resource::AssetManager>::Get();
    // Resource::AssetId cubeId = am->MakeAssetId("engine://Image/Table_Defringed_4k2k.hdr");
    Resource::AssetId cubeId = am->MakeAssetId("engine://Image/cobblestone_parish_road_2k.hdr");

    Resource::ImageAssetLoader loader;
    UniquePtr<Resource::AssetData> rawData = loader.LoadSource(cubeId, *fileSystem);
    if (!rawData)
    {
        LOG_ERROR("[BakeCubemap] Failed to load the equirect HDRI as raw pixels.");
        return 1;
    }
    const auto& raw = static_cast<const Resource::ImageAssetRawData&>(*rawData);

    // --- Bake the full environment (sky + irradiance + prefiltered) ---
    const uint32_t faceSize = 1024; // matches DefaultHDRDescriptor's cubemapFaceSize
    Resource::BakedEnvironment env = baker.Bake(raw, faceSize);

    // --- Verify shapes ---
    bool ok = true;
    // Shapes come from the baker's own constants: hardcoding them here just means this
    // check goes stale the first time a bake parameter is tuned.
    ok &= VerifyCube(env.sky, faceSize, ExpectedMipLevels(faceSize), "sky");
    ok &= VerifyCube(env.irradiance, Resource::EnvironmentBaker::kIrradianceSize, 1, "irradiance");
    ok &= VerifyCube(env.prefiltered, Resource::EnvironmentBaker::PrefilterFaceSize(faceSize),
                     Resource::EnvironmentBaker::kPrefilterMips, "prefiltered");
    if (!ok)
    {
        return 1;
    }

    // --- Dump strips for visual inspection ---
    DumpCubeStrips(env.sky, "bake_sky");
    DumpCubeStrips(env.irradiance, "bake_irr");
    DumpCubeStrips(env.prefiltered, "bake_pref");

    // --- Asset layer: the bake above verifies the baker, this one verifies the plumbing ---
    if (!VerifyAssetLayer(*assetManager, cubeId))
    {
        return 1;
    }

    // --- Unit cache: the run above filled it, a second manager must restore all three ---
    // Sequential, never two managers at once: Service<AssetManager> names only one, so a
    // second live manager would tear this one's assets out of its database.
    assetManager.reset();
    if (!VerifyUnitCache(*fileSystem, cubeId, cacheDir))
    {
        return 1;
    }

    LOG_INFO("[BakeCubemap] Done. sky strips should read as a progressive blur top to "
             "bottom; irradiance strips should be a soft low-frequency ambient colour with "
             "no high-frequency detail; prefiltered mip 0 should be near-sharp and each "
             "successive level markedly blurrier (each an independent GGX integral, not a "
             "plain downsample).");
    return 0;
}
