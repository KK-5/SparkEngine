#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include <Resource/AssetTypes.h>

#include "Components.h"

namespace Spark::Material
{
    static void Reflect(Spark::ReflectContext& context)
    {
        // The material's authored parameters. NOT a world component — it lives on
        // material entities in the MaterialContext. Registered only for its field
        // metadata, which the editor's MaterialRefElement expands inline when a
        // primitive's MaterialComponent is inspected. No ComponentOpertion, so it
        // never binds to WorldContext / shows up as a standalone world component.
        context.Reflect<MaterialParams>()
            .Type("MaterialParams")
            .Data<&MaterialParams::m_baseColor>("Base Color").Custom<Spark::ColorElement>(false)
            .Data<&MaterialParams::m_metallic>("Metallic").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            .Data<&MaterialParams::m_roughness>("Roughness").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            .Data<&MaterialParams::m_specular>("Specular").Custom<Spark::FloatSliderElement>(0.f, 1.f, 0.01f, false)
            .Data<&MaterialParams::m_baseColorTexture>("Base Color Map")
                .Custom<Spark::AssetElement>(false, static_cast<uint32_t>(Resource::AssetType::Image))
            ;

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
