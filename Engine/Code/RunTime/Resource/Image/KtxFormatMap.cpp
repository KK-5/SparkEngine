#include "KtxFormatMap.h"

namespace Spark::Resource
{
    namespace
    {
        struct FormatPair
        {
            RHI::Format rhi;
            uint32_t    vk;
        };

        //! Numbers are the Vulkan core enum's, fixed by the spec, so they are spelled out
        //! rather than pulled in through a Vulkan header this module does not otherwise need.
        //! BC1_UNORM pairs with BC1_RGBA: DXGI has no RGB/RGBA split, its BC1 always carries
        //! the 1-bit alpha.
        constexpr FormatPair kFormats[] = {
            {RHI::Format::R8_UNORM,             9},   // R8_UNORM
            {RHI::Format::R8G8_UNORM,           16},  // R8G8_UNORM
            {RHI::Format::R8G8B8A8_UNORM,       37},  // R8G8B8A8_UNORM
            {RHI::Format::R8G8B8A8_UNORM_SRGB,  43},  // R8G8B8A8_SRGB
            {RHI::Format::R16G16_FLOAT,         83},  // R16G16_SFLOAT
            {RHI::Format::R16G16B16A16_FLOAT,   97},  // R16G16B16A16_SFLOAT -- every bake product
            {RHI::Format::R32G32B32A32_FLOAT,   109}, // R32G32B32A32_SFLOAT
            {RHI::Format::BC1_UNORM,            133}, // BC1_RGBA_UNORM_BLOCK
            {RHI::Format::BC1_UNORM_SRGB,       134}, // BC1_RGBA_SRGB_BLOCK
            {RHI::Format::BC3_UNORM,            137}, // BC3_UNORM_BLOCK
            {RHI::Format::BC3_UNORM_SRGB,       138}, // BC3_SRGB_BLOCK
            {RHI::Format::BC4_UNORM,            139}, // BC4_UNORM_BLOCK
            {RHI::Format::BC4_SNORM,            140}, // BC4_SNORM_BLOCK
            {RHI::Format::BC5_UNORM,            141}, // BC5_UNORM_BLOCK
            {RHI::Format::BC5_SNORM,            142}, // BC5_SNORM_BLOCK
            {RHI::Format::BC6H_UF16,            143}, // BC6H_UFLOAT_BLOCK
            {RHI::Format::BC6H_SF16,            144}, // BC6H_SFLOAT_BLOCK
            {RHI::Format::BC7_UNORM,            145}, // BC7_UNORM_BLOCK
            {RHI::Format::BC7_UNORM_SRGB,       146}, // BC7_SRGB_BLOCK
        };
    }

    uint32_t ToVkFormat(RHI::Format format)
    {
        for (const FormatPair& pair : kFormats)
        {
            if (pair.rhi == format)
            {
                return pair.vk;
            }
        }
        return kVkFormatUndefined;
    }

    RHI::Format FromVkFormat(uint32_t vkFormat)
    {
        for (const FormatPair& pair : kFormats)
        {
            if (pair.vk == vkFormat)
            {
                return pair.rhi;
            }
        }
        return RHI::Format::Unknown;
    }
}
