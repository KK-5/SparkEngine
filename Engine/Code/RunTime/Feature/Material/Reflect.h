#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include <Resource/AssetTypes.h>

#include "Components.h"
#include "MaterialContext.h"

namespace Spark::Material
{
    //! Per-slot texture accessors. The texture asset ids live in MaterialParams::m_textures
    //! (an eastl::array indexed by MaterialTexSlot), so they have no &Class::member pointer
    //! to reflect directly — these getter/setter pairs expose each slot as its own reflected
    //! field, reusing the editor's AssetElement + drag-drop path (which writes via the
    //! reflected field's setter). Adding a channel = add one Data<Set,Get> line below.
    template<MaterialTexSlot Slot>
    Resource::AssetId GetTexAsset(const MaterialParams& params)
    {
        return params.m_textures[static_cast<size_t>(Slot)].m_assetId;
    }

    template<MaterialTexSlot Slot>
    void SetTexAsset(MaterialParams& params, Resource::AssetId id)
    {
        params.m_textures[static_cast<size_t>(Slot)].m_assetId = id;
    }

    static void Reflect(Spark::ReflectContext& context)
    {
        constexpr uint32_t kImageType = static_cast<uint32_t>(Resource::AssetType::Image);
        // The material's authored parameters. NOT a world component — it lives on
        // material entities in the MaterialContext, reached indirectly (rendered inline
        // by MaterialRefElement when a primitive's MaterialComponent is inspected).
        // ComponentOperation binds it to the MaterialContext with IsWorld=false, so the
        // editor's HasComponent/GetComponent/ReplaceComponent resolve MaterialContext
        // internally (no context ever reaches the editor) and the inspector does not list
        // it as a standalone world component.
        context.Reflect<MaterialParams>()
            .Type("MaterialParams")
            // Scalar / color factors.
            .Data<&MaterialParams::m_baseColor>("Base Color").Custom<Spark::ColorElement>(false)
            .Data<&MaterialParams::m_metallic>("Metallic").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            .Data<&MaterialParams::m_roughness>("Roughness").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            .Data<&MaterialParams::m_specular>("Specular").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            .Data<&MaterialParams::m_emissive>("Emissive").Custom<Spark::Vec3Element>(0.f, 64.f, 0.01f, false)
            .Data<&MaterialParams::m_emissiveStrength>("Emissive Strength").Custom<Spark::FloatElement>(0.f, 100.f, 0.05f, false)
            .Data<&MaterialParams::m_normalScale>("Normal Scale").Custom<Spark::FloatSliderElement>(0.f, 2.f, 0.01f, false)
            .Data<&MaterialParams::m_occlusionStrength>("Occlusion Strength").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            // Texture slots — one reflected field per MaterialTexSlot via getter/setter.
            .Data<&SetTexAsset<MaterialTexSlot::BaseColor>, &GetTexAsset<MaterialTexSlot::BaseColor>>("Base Color Map")
                .Custom<Spark::AssetElement>(false, kImageType)
            .Data<&SetTexAsset<MaterialTexSlot::MetallicRoughness>, &GetTexAsset<MaterialTexSlot::MetallicRoughness>>("Metallic-Roughness Map")
                .Custom<Spark::AssetElement>(false, kImageType)
            .Data<&SetTexAsset<MaterialTexSlot::Normal>, &GetTexAsset<MaterialTexSlot::Normal>>("Normal Map")
                .Custom<Spark::AssetElement>(false, kImageType)
            .Data<&SetTexAsset<MaterialTexSlot::Occlusion>, &GetTexAsset<MaterialTexSlot::Occlusion>>("Occlusion Map")
                .Custom<Spark::AssetElement>(false, kImageType)
            .Data<&SetTexAsset<MaterialTexSlot::Emissive>, &GetTexAsset<MaterialTexSlot::Emissive>>("Emissive Map")
                .Custom<Spark::AssetElement>(false, kImageType)
            ;
        Spark::ComponentOperation<MaterialExecuteContext, MaterialHandle, MaterialParams>(context);

        // The world-side reference held by a primitive entity. A normal editable
        // world component; its single MaterialHandle field uses MaterialRefElement so
        // the editor follows the reference into the MaterialContext and renders the
        // referenced MaterialParams inline (recursive field expansion).
        context.Reflect<MaterialComponent>()
            .Type("Material").Custom<ComponentTraitsRuntime>(ComponentTraits<MaterialComponent>{})
            .Data<&MaterialComponent::m_material>("Material").Custom<Spark::MaterialRefElement>(false)
            ;

        Spark::ComponentOpertion<MaterialComponent>(context);
    }
}
