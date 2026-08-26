#pragma once

#include <cstdint>

#include <RHI/Format.h>

namespace Spark::Resource
{
    //! One table, both directions. KTX2 stores a VkFormat whatever API reads the file, so
    //! this is the engine's only place that speaks Vulkan's enum. Unlisted formats map to
    //! Unknown / UNDEFINED for the caller to reject -- a wrong number opens fine and decodes
    //! to garbage, so nothing is guessed.
    uint32_t    ToVkFormat(RHI::Format format);
    RHI::Format FromVkFormat(uint32_t vkFormat);

    constexpr uint32_t kVkFormatUndefined = 0;
}
