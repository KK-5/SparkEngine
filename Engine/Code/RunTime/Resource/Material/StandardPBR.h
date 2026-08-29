#pragma once

#include <cstdint>

#include <EASTL/array.h>

#include <Math/Vector4.h>

#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    //! Texture channels of the standard PBR metallic-roughness material. The enum
    //! value IS the index into StandardPBR::m_textures / MaterialGPUTextures /
    //! the GPU MaterialData index array — so the ORDER IS A GPU CONTRACT. Never
    //! reorder or insert in the middle; append new channels before Count only.
    //! Count is the array-size sentinel and must stay last.
    enum class MaterialTexSlot : uint8_t
    {
        BaseColor         = 0,   // sRGB; albedo, .a = alpha
        MetallicRoughness = 1,   // linear; glTF packing G=roughness, B=metallic
        Normal            = 2,   // linear; tangent-space
        Occlusion         = 3,   // linear; R channel (often ORM-packed with MR)
        Emissive          = 4,   // sRGB

        Count
    };

    inline constexpr size_t MaterialTexSlotCount = static_cast<size_t>(MaterialTexSlot::Count);

    //! The authored parameter set of the StandardPBR shading model. The type's reflected
    //! name is also the shading model's name in a `.smat` and this component's key in a
    //! scene file, so it is frozen once either has been written.
    //!
    //! It lives in the asset layer rather than with the material system because both the
    //! `.smat` builder and the model builder construct one, and SparkAssetManager cannot
    //! depend on a Feature module. The material system stores it as a component on the
    //! material entity; nothing here knows that.
    struct StandardPBR
    {
        // Scalar / color inputs — packed into the GPU MaterialData struct. Each pairs
        // with a texture slot below (the map modulates / overrides the factor); the
        // factor is also the fallback used when the slot has no map.
        Math::Vector4 m_baseColor         = {0.8f, 0.8f, 0.8f, 1.0f};   // rgba; .a = alpha  (BaseColor)
        float         m_metallic          = 0.0f;            // metalness          (MetallicRoughness.b)
        float         m_roughness         = 0.5f;            // perceptual         (MetallicRoughness.g)
        float         m_specular          = 0.5f;            // dielectric F0 scale; 0.5 -> F0 0.04
        Math::Vector4 m_emissive          = {0.0f, 0.0f, 0.0f, 1.0f}; // rgb = emissive color; .a unused (Emissive)
        float         m_emissiveStrength  = 1.0f;            // HDR emissive multiplier
        float         m_normalScale       = 1.0f;            // tangent XY scale   (Normal)
        float         m_occlusionStrength = 1.0f;            // AO lerp strength   (Occlusion)

        // Texture inputs — slot-indexed, driven uniformly by MaterialTextureSystem.
        // An invalid slot means "no map" (shader falls back to the factor above).
        // Authored together with the scalars: e.g. the "Base Color" input is
        // m_baseColor + m_textures[BaseColor].
        eastl::array<AssetId, MaterialTexSlotCount> m_textures;
    };
}
