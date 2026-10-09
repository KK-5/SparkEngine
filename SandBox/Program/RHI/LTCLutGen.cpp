// LTCLutGen — imports the two Linearly Transformed Cosines tables rect lights are shaded with.
//
// Nothing is baked here: fitting the tables is a nonlinear optimisation per texel, and the
// reference implementation publishes its result. This reads that result (fit/results/ltc_1.dds
// and ltc_2.dds of https://github.com/selfshadow/ltc_code), checks it is laid out the way the
// shader reads it, and rewrites it as the .ktx2 the asset system loads. The two outputs are
// checked into Engine/Asset/Image/; the .dds files are not.
//
//     LTCLutGen <path to ltc_1.dds> <path to ltc_2.dds>
//
// What the tables hold and how they are addressed: Document/TODO_AreaLightPlan.md, section 1.5.
//
// The tables are the work of the paper's authors, under this license:
//
//   Copyright (c) 2017, Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt.
//   All rights reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * If you use (or adapt) the source code in your own work, please include a
//     reference to the paper:
//
//     Real-Time Polygonal-Light Shading with Linearly Transformed Cosines.
//     Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt.
//     ACM Transactions on Graphics (Proceedings of ACM SIGGRAPH 2016) 35(4), 2016.
//     Project page: https://eheitzresearch.wordpress.com/415-2/
//
//   * Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>

#include <Resource/AssetManager.h>
#include <VFS/VFSSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

#include <ktx.h>

#include <EASTL/vector.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace Spark;

namespace
{
    constexpr uint32_t    kSize    = 64;
    constexpr RHI::Format kFormat  = RHI::Format::R16G16B16A16_FLOAT;
    constexpr uint32_t    kBytesPP = 8;
    constexpr size_t      kPayloadBytes = static_cast<size_t>(kSize) * kSize * kBytesPP;

    constexpr uint32_t kVkFormatR16G16B16A16_SFLOAT = 97;

    // The reference writes a plain DDS_HEADER with no DX10 extension.
    constexpr uint32_t kDdsMagic       = 0x20534444;   // "DDS "
    constexpr uint32_t kDdsHeaderBytes = 128;
    constexpr uint32_t kDdsHeightAt    = 12;
    constexpr uint32_t kDdsWidthAt     = 16;
    constexpr uint32_t kDdsFourCCAt    = 84;
    constexpr uint32_t kFourCCRGBA16F  = 113;          // D3DFMT_A16B16G16R16F

    struct Output
    {
        const char* m_file;
        const char* m_asset;
    };

    constexpr Output kLTC1 = { ENGINE_ASSET_DIR "/Image/LTC1.ktx2", "engine://Image/LTC1.ktx2" };
    constexpr Output kLTC2 = { ENGINE_ASSET_DIR "/Image/LTC2.ktx2", "engine://Image/LTC2.ktx2" };

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
                bits = sign;
            }
            else
            {
                exp = 1;
                while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
                mant &= 0x3FFu;
                bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
            }
        }
        else if (exp == 0x1Fu)
        {
            bits = sign | 0x7F800000u | (mant << 13);
        }
        else
        {
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }

        float out;
        memcpy(&out, &bits, sizeof(out));
        return out;
    }

    uint32_t ReadU32(const eastl::vector<uint8_t>& bytes, size_t offset)
    {
        uint32_t value;
        memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    bool ReadDdsPayload(const char* path, eastl::vector<uint8_t>& payload)
    {
        FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || !file)
        {
            LOG_ERROR("[LTCLutGen] cannot open {}", path);
            return false;
        }

        eastl::vector<uint8_t> bytes(kDdsHeaderBytes + kPayloadBytes + 1);
        const size_t read = fread(bytes.data(), 1, bytes.size(), file);
        fclose(file);

        if (read != kDdsHeaderBytes + kPayloadBytes
            || ReadU32(bytes, 0) != kDdsMagic
            || ReadU32(bytes, kDdsHeightAt) != kSize
            || ReadU32(bytes, kDdsWidthAt) != kSize
            || ReadU32(bytes, kDdsFourCCAt) != kFourCCRGBA16F)
        {
            LOG_ERROR("[LTCLutGen] {} is not a {}x{} RGBA16F DDS without a DX10 header.",
                      path, kSize, kSize);
            return false;
        }

        payload.assign(bytes.begin() + kDdsHeaderBytes, bytes.begin() + kDdsHeaderBytes + kPayloadBytes);
        return true;
    }

    float Channel(const eastl::vector<uint8_t>& table, uint32_t x, uint32_t y, uint32_t channel)
    {
        uint16_t half;
        memcpy(&half, table.data() + (static_cast<size_t>(y) * kSize + x) * kBytesPP + channel * 2,
               sizeof(half));
        return HalfToFloat(half);
    }

    bool Near(float value, float expected)
    {
        return fabsf(value - expected) < 0.01f;
    }

    //! Columns are roughness and rows are the view angle, row 0 being head-on. There the lobe
    //! is symmetric about the normal, so the inverse matrix is (1, 0, 0, w) for every
    //! roughness — a table with its axes swapped has y and z in that row, and the two files
    //! given in the wrong order fail both tests. Neither mistake shows anywhere else until a
    //! surface is lit.
    bool CheckLayout(const eastl::vector<uint8_t>& ltc1, const eastl::vector<uint8_t>& ltc2)
    {
        const uint32_t rough = kSize - 1;

        const float m[4] = { Channel(ltc1, rough, 0, 0), Channel(ltc1, rough, 0, 1),
                             Channel(ltc1, rough, 0, 2), Channel(ltc1, rough, 0, 3) };

        // 1 - ln 2: the albedo of GGX at alpha 1 under height-correlated Smith, seen head-on,
        // has a closed form. Matching it is also what says the tables were fitted to the
        // masking term the engine shades with.
        const float albedoRough  = Channel(ltc2, rough, 0, 0);
        const float albedoSmooth = Channel(ltc2, 0, 0, 0);

        LOG_INFO("[LTCLutGen] rough/head-on: inverse matrix = ({:.3f}, {:.3f}, {:.3f}, {:.3f}), "
                 "albedo = {:.4f}; smooth/head-on albedo = {:.4f}",
                 m[0], m[1], m[2], m[3], albedoRough, albedoSmooth);

        if (!Near(m[0], 1.0f) || !Near(m[1], 0.0f) || !Near(m[2], 0.0f) || !Near(m[3], 1.128f))
        {
            LOG_ERROR("[LTCLutGen] the first file is not ltc_1 in the expected layout.");
            return false;
        }
        if (!Near(albedoRough, 0.3069f) || !Near(albedoSmooth, 1.0f))
        {
            LOG_ERROR("[LTCLutGen] the second file is not ltc_2 in the expected layout.");
            return false;
        }
        return true;
    }

    bool WriteKtx2(const char* path, const eastl::vector<uint8_t>& bytes)
    {
        ktxTextureCreateInfo info{};
        info.vkFormat        = kVkFormatR16G16B16A16_SFLOAT;
        info.baseWidth       = kSize;
        info.baseHeight      = kSize;
        info.baseDepth       = 1;
        info.numDimensions   = 2;
        info.numLevels       = 1;
        info.numLayers       = 1;
        info.numFaces        = 1;
        info.generateMipmaps = KTX_FALSE;

        ktxTexture2*   tex = nullptr;
        KTX_error_code res = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex);
        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[LTCLutGen] ktxTexture2_Create failed: {}", static_cast<int>(res));
            return false;
        }

        res = ktxTexture_SetImageFromMemory(ktxTexture(tex), 0, 0, 0,
                                            bytes.data(), static_cast<ktx_size_t>(bytes.size()));
        if (res == KTX_SUCCESS)
        {
            res = ktxTexture_WriteToNamedFile(ktxTexture(tex), path);
        }
        ktxTexture_Destroy(ktxTexture(tex));

        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[LTCLutGen] failed to write {}: {}", path, static_cast<int>(res));
            return false;
        }
        LOG_INFO("[LTCLutGen] wrote {} ({}x{} RGBA16F, {}B payload)", path, kSize, kSize, bytes.size());
        return true;
    }

    //! Through AssetManager, since that is the path the renderer loads them by.
    bool VerifyRoundTrip(const char* assetPath, const eastl::vector<uint8_t>& expected)
    {
        auto* am = Service<Resource::AssetManager>::Get();
        Ptr<Resource::Asset> asset = am->LoadAsset(am->MakeAssetId(assetPath));
        if (!asset || !asset->IsReady())
        {
            LOG_ERROR("[LTCLutGen] round trip: {} is not Ready.", assetPath);
            return false;
        }

        const auto* data = static_cast<const Resource::ImageAsset*>(asset.get())->GetImageData();
        if (!data || data->GetWidth() != kSize || data->GetHeight() != kSize
            || data->GetMipLevels() != 1 || data->GetArrayLayers() != 1
            || data->GetFormat() != kFormat)
        {
            LOG_ERROR("[LTCLutGen] round trip: {} shape/format mismatch.", assetPath);
            return false;
        }
        if (data->GetTextureBytes().size() != expected.size()
            || memcmp(data->GetTextureBytes().data(), expected.data(), expected.size()) != 0)
        {
            LOG_ERROR("[LTCLutGen] round trip: {} differs from what was written.", assetPath);
            return false;
        }

        LOG_INFO("[LTCLutGen] round trip OK: {}", assetPath);
        return true;
    }
}

int main(int argc, char** argv)
{
    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem> logger = eastl::make_unique<SpdLogSystem>(logConfig);

    if (argc != 3)
    {
        LOG_ERROR("[LTCLutGen] usage: LTCLutGen <ltc_1.dds> <ltc_2.dds>");
        return 1;
    }

    eastl::vector<uint8_t> ltc1;
    eastl::vector<uint8_t> ltc2;
    if (!ReadDdsPayload(argv[1], ltc1) || !ReadDdsPayload(argv[2], ltc2))
    {
        return 1;
    }
    if (!CheckLayout(ltc1, ltc2))
    {
        return 1;
    }

    auto fileSystem = CreateSystem<VFSSystem>();
    fileSystem->Init();
    fileSystem->Mount("engine", ENGINE_ASSET_DIR);

    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();

    if (!WriteKtx2(kLTC1.m_file, ltc1) || !WriteKtx2(kLTC2.m_file, ltc2))
    {
        return 1;
    }
    if (!VerifyRoundTrip(kLTC1.m_asset, ltc1) || !VerifyRoundTrip(kLTC2.m_asset, ltc2))
    {
        return 1;
    }

    LOG_INFO("[LTCLutGen] Done.");
    return 0;
}
