#pragma once

#include <cstdint>

#include <Math/Bit.h>

#include <Resource/Material/MaterialState.h>

namespace Spark::Render
{
    //! What can make one instance's PSO differ from another's in some pass, packed into one
    //! integer: the feature bits low, the shader index above them. The layout is known here
    //! only; everything else goes through the functions below.
    //!
    //! Also the component on a key's entity, so the value and the entity share one type.
    struct PipelineKey
    {
        uint32_t m_value = 0;

        bool operator==(const PipelineKey& rhs) const { return m_value == rhs.m_value; }
        bool operator!=(const PipelineKey& rhs) const { return m_value != rhs.m_value; }
    };

    namespace PipelineKeyLayout
    {
        // Room for the alpha modes still to be appended.
        inline constexpr uint32_t AlphaModeShift   = 0;
        inline constexpr uint32_t AlphaModeBits    = 3;
        inline constexpr uint32_t DoubleSidedShift = AlphaModeShift + AlphaModeBits;
        inline constexpr uint32_t FeatureBitCount  = DoubleSidedShift + 1;
        inline constexpr uint32_t ShaderIndexShift = FeatureBitCount;

        static_assert(static_cast<uint32_t>(Resource::AlphaMode::AlphaComposite) <= BIT_MASK(AlphaModeBits),
            "AlphaMode outgrew its bits in PipelineKey.");
    }

    inline constexpr uint32_t FeatureCombinationCount = BIT(PipelineKeyLayout::FeatureBitCount);

    //! Material shaders a key can name. One until materials reference shaders of their own.
    inline constexpr uint32_t PipelineShaderCount = 1;

    //! Every key is below it: a table addressed by key has this many entries.
    inline constexpr uint32_t PipelineKeyCount = PipelineShaderCount * FeatureCombinationCount;

    inline PipelineKey MakePipelineKey(uint32_t shaderIndex, const Resource::MaterialState& state)
    {
        using namespace PipelineKeyLayout;
        uint32_t value = (shaderIndex << ShaderIndexShift)
            | (static_cast<uint32_t>(state.m_alphaMode) << AlphaModeShift);
        if (state.m_doubleSided)
        {
            value = SetBit(value, DoubleSidedShift);
        }
        return PipelineKey{ value };
    }

    inline uint32_t GetShaderIndex(PipelineKey key)
    {
        return key.m_value >> PipelineKeyLayout::ShaderIndexShift;
    }

    inline Resource::AlphaMode GetAlphaMode(PipelineKey key)
    {
        using namespace PipelineKeyLayout;
        return static_cast<Resource::AlphaMode>((key.m_value >> AlphaModeShift) & BIT_MASK(AlphaModeBits));
    }

    inline bool IsDoubleSided(PipelineKey key)
    {
        return CheckBit(key.m_value, PipelineKeyLayout::DoubleSidedShift);
    }
}
