#pragma once

#include <cstdint>

#include <EASTL/array.h>

#include <Math/Vector3.h>
#include <Math/Vector4.h>
#include <ECS/ComponentTraits.h>
#include <RHI/Context/RHIHandle.h>
#include <Resource/AssetTypes.h>
#include <Resource/Image/ImageAsset.h>

#include "MaterialHandle.h"

namespace Spark::Material
{
    //! Texture channels of the standard PBR metallic-roughness material. The enum
    //! value IS the index into MaterialParams::m_textures / MaterialGPUTextures /
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

    //! One texture channel: the authored asset id plus a resolve cache. m_image is
    //! runtime state owned by MaterialTextureSystem (populated when the asset resolves
    //! to Ready), NOT authored data — it is not serialized.
    struct MaterialTexture
    {
        Resource::AssetId         m_assetId;
        Ptr<Resource::ImageAsset> m_image;
    };

    struct MaterialParams
    {
        // Scalar / color inputs — packed into the GPU MaterialData struct. Each pairs
        // with a texture slot below (the map modulates / overrides the factor); the
        // factor is also the fallback used when the slot has no map.
        Math::Vector4 m_baseColor         = {0.8f, 0.8f, 0.8f, 1.0f};   // rgba; .a = alpha  (BaseColor)
        float         m_metallic          = 0.0f;            // metalness          (MetallicRoughness.b)
        float         m_roughness         = 0.5f;            // perceptual         (MetallicRoughness.g)
        float         m_specular          = 0.5f;            // dielectric F0 scale; 0.5 -> F0 0.04
        Math::Vector3 m_emissive          = {0.0f, 0.0f, 0.0f}; // emissive color   (Emissive)
        float         m_emissiveStrength  = 1.0f;            // HDR emissive multiplier
        float         m_normalScale       = 1.0f;            // tangent XY scale   (Normal)
        float         m_occlusionStrength = 1.0f;            // AO lerp strength   (Occlusion)

        // Texture inputs — slot-indexed, driven uniformly by MaterialTextureSystem.
        // A slot with an invalid m_assetId means "no map" (shader falls back to the
        // factor above). Authored together with the scalars: e.g. the "Base Color"
        // input is m_baseColor + m_textures[BaseColor].
        eastl::array<MaterialTexture, MaterialTexSlotCount> m_textures;
    };

    struct MaterialComponent
    {
        MaterialHandle m_material{NullMaterial};
    };

    //! Resolved per-channel GPU textures (RHIHandles) on the material entity, one per
    //! MaterialTexSlot. Written by MaterialTextureSystem (owned by MaterialSystem), read
    //! by render's MaterialBindingSystem to resolve each bindless index. NullHandle in a
    //! slot = unresolved / no map. This is the producer→consumer handoff component, so it
    //! lives in SparkMaterial (the producer's module); render depends on it and includes it.
    struct MaterialGPUTextures
    {
        eastl::array<RHI::RHIHandle, MaterialTexSlotCount> m_handles;

        MaterialGPUTextures()
        {
            m_handles.fill(RHI::NullHandle);
        }
    };

    //! Marks the one resident default material in the MaterialContext. Lets any
    //! consumer discover it by querying the context (data-driven, no factory API) —
    //! the fallback target for unset/dangling MaterialComponent references (§1.5).
    struct DefaultMaterialTag {};
}

namespace Spark
{
    // Editable so it appears in the inspector's add-component list. Create event so
    // MaterialSystem can auto-create a private material on add (Remove/GC deferred —
    // materials are not destroyed yet).
    SPARK_COMPONENT_TRAITS(Material::MaterialComponent,
        static constexpr bool editable = true;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::Create;
    )
}
