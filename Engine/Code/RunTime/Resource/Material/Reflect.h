#pragma once

#include <Reflection/ReflectContext.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>

#include <Resource/Image/ImageAsset.h>   // ImageUsage — per-slot texture load usage

#include "MaterialState.h"
#include "StandardPBR.h"

namespace Spark::Resource
{
    //! The `state` half of a `.smat`. Same rule as the properties below: every name here
    //! is on-disk format, AlphaMode's enumerator names included.
    static void ReflectMaterialState(Spark::ReflectContext& context)
    {
        context.Reflect<AlphaMode>()
            .Type("AlphaMode")
            .Data<AlphaMode::Opaque>("Opaque")
            .Data<AlphaMode::Mask>("Mask")
            .Data<AlphaMode::Blend>("Blend");

        context.Reflect<MaterialState>()
            .Type("MaterialState")
            .Data<&MaterialState::m_alphaMode>("Alpha Mode").Custom<Spark::EnumElement>(false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&MaterialState::m_alphaCutoff>("Alpha Cutoff").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&MaterialState::m_doubleSided>("Double Sided").Custom<Spark::BoolElement>(false)
                .Traits(MetaFieldTraits::Serializable)
            ;
    }

    //! Per-slot texture accessors. The texture asset ids live in StandardPBR::m_textures
    //! (an eastl::array indexed by MaterialTexSlot), so they have no &Class::member pointer
    //! to reflect directly — these getter/setter pairs expose each slot as its own reflected
    //! field, reusing the editor's AssetElement + drag-drop path (which writes via the
    //! reflected field's setter). Adding a channel = add one Data<Set,Get> line below.
    //!
    //! Params is the concrete type being reflected, not always StandardPBR: entt resolves a
    //! free accessor against the exact type it is registered on, so a type deriving from
    //! StandardPBR needs its own instantiation.
    template<typename Params, MaterialTexSlot Slot>
    AssetId GetTexAsset(const Params& params)
    {
        return params.m_textures[static_cast<size_t>(Slot)];
    }

    template<typename Params, MaterialTexSlot Slot>
    void SetTexAsset(Params& params, AssetId id)
    {
        params.m_textures[static_cast<size_t>(Slot)] = id;
    }

    //! StandardPBR's field layout: the names, the editor widgets, and what reaches a file.
    //! It sits in the asset layer with the struct because a `.smat` is read and written
    //! here; binding the struct to the MaterialContext as a component is the material
    //! system's half, and stays there.
    //!
    //! Every name below is on-disk format — the `.smat` `properties` keys and the scene
    //! file's field keys are these strings. Renaming one silently drops the value from
    //! every file that already spells it.
    //!
    //! Templated because StandardPBR is not the only type with this layout: a per-object
    //! override carries the same fields under its own type name, and entt's field range
    //! does not visit a base class, so it needs them registered on itself.
    template<typename Params>
    void ReflectStandardPBRFields(Spark::ReflectContext& context, const char* typeName)
    {
        // Per-slot texture usage — decides the color space the dropped image is compiled
        // with (sRGB color vs linear data vs normal map). See TextureElement.
        constexpr uint32_t kUsageColor  = static_cast<uint32_t>(ImageUsage::Texture2D);
        constexpr uint32_t kUsageData   = static_cast<uint32_t>(ImageUsage::NoColorTexture2D);
        constexpr uint32_t kUsageNormal = static_cast<uint32_t>(ImageUsage::NormalMap);

        using Slot = MaterialTexSlot;

        context.Reflect<Params>()
            .Type(typeName)
            // Scalar / color factors.
            .Data<&Params::m_baseColor>("Base Color").Custom<Spark::ColorElement>(false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_metallic>("Metallic").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_roughness>("Roughness").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_specular>("Specular").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_emissive>("Emissive Color").Custom<Spark::ColorElement>(false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_emissiveStrength>("Emissive Strength").Custom<Spark::FloatElement>(0.f, 100.f, 0.05f, false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_normalScale>("Normal Scale").Custom<Spark::FloatSliderElement>(0.f, 2.f, 0.01f, false)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&Params::m_occlusionStrength>("Occlusion Strength").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
                .Traits(MetaFieldTraits::Serializable)
            // Texture slots — one reflected field per MaterialTexSlot via getter/setter.
            // TextureElement carries the slot's ImageUsage so a dropped image is loaded with
            // the right color space (base color / emissive sRGB; MR / occlusion linear data;
            // normal a linear normal map).
            .Data<&SetTexAsset<Params, Slot::BaseColor>, &GetTexAsset<Params, Slot::BaseColor>>("Base Color Map")
                .Custom<Spark::TextureElement>(false, kUsageColor)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&SetTexAsset<Params, Slot::MetallicRoughness>, &GetTexAsset<Params, Slot::MetallicRoughness>>("Metallic Roughness Map")
                .Custom<Spark::TextureElement>(false, kUsageData)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&SetTexAsset<Params, Slot::Normal>, &GetTexAsset<Params, Slot::Normal>>("Normal Map")
                .Custom<Spark::TextureElement>(false, kUsageNormal)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&SetTexAsset<Params, Slot::Occlusion>, &GetTexAsset<Params, Slot::Occlusion>>("Occlusion Map")
                .Custom<Spark::TextureElement>(false, kUsageData)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&SetTexAsset<Params, Slot::Emissive>, &GetTexAsset<Params, Slot::Emissive>>("Emissive Map")
                .Custom<Spark::TextureElement>(false, kUsageColor)
                .Traits(MetaFieldTraits::Serializable)
            ;
    }

    static void ReflectStandardPBR(Spark::ReflectContext& context)
    {
        ReflectStandardPBRFields<StandardPBR>(context, "StandardPBR");
    }
}
