#pragma once

#include <cstdint>

#include <EASTLEX/hash.h>
#include <Math/Bit.h>

#include <RHI/Pipeline/RenderStates.h>

namespace Spark::Render
{
    //! A feature's shader macro. Every pass template follows the same macros, so a macro is
    //! the feature's, never a pass's own permutation.
    enum class FeatureMacro : uint32_t
    {
        None = 0,
        //! MATERIALBLENDING_MASKED: the template clips by the material's opacity.
        MaterialBlendingMasked = BIT(0),
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::Render::FeatureMacro, uint32_t);

    //! What a pass changes from its default PSO for one pipeline key. Compared by value: equal
    //! variants of one pass draw in one bin.
    struct PipelineVariant
    {
        static constexpr uint32_t NoMaterialShader = UINT32_MAX;

        //! A pass draws its bins lowest first.
        uint32_t      m_priority       = 0;
        //! Invalid keeps the pass's own.
        RHI::CullMode m_cullMode       = RHI::CullMode::Invalid;
        FeatureMacro  m_featureMacros  = FeatureMacro::None;
        //! The material shader built into the template, or none: the template's own code.
        uint32_t      m_materialShader = NoMaterialShader;

        bool operator==(const PipelineVariant& rhs) const
        {
            return m_priority == rhs.m_priority
                && m_cullMode == rhs.m_cullMode
                && m_featureMacros == rhs.m_featureMacros
                && m_materialShader == rhs.m_materialShader;
        }

        bool operator!=(const PipelineVariant& rhs) const { return !(*this == rhs); }
    };
}

namespace eastl
{
    template<>
    struct hash<Spark::Render::PipelineVariant>
    {
        size_t operator()(const Spark::Render::PipelineVariant& variant) const
        {
            size_t seed = 0;
            hash_combine(seed, variant.m_priority, static_cast<uint32_t>(variant.m_cullMode),
                static_cast<uint32_t>(variant.m_featureMacros), variant.m_materialShader);
            return seed;
        }
    };
}
