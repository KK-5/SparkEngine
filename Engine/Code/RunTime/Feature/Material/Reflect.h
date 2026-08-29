#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "Components.h"
#include "MaterialContext.h"

namespace Spark::Material
{
    static void Reflect(Spark::ReflectContext& context)
    {
        // StandardPBR's fields are reflected by the asset layer (Resource/Material/Reflect.h);
        // what belongs here is the binding that makes it a component. NOT a world component —
        // it lives on material entities in the MaterialContext, reached indirectly (rendered
        // inline by MaterialRefElement when a primitive's MaterialComponent is inspected).
        // ComponentOperation binds it with IsWorld=false, so the editor's
        // HasComponent/GetComponent/ReplaceComponent resolve MaterialContext internally (no
        // context ever reaches the editor) and the inspector does not list it as a standalone
        // world component.
        Spark::ComponentOperation<MaterialExecuteContext, MaterialHandle, Resource::StandardPBR>(context);

        // The world-side reference held by a primitive entity. A normal editable
        // world component; its single MaterialHandle field uses MaterialRefElement so
        // the editor follows the reference into the MaterialContext and renders the
        // referenced StandardPBR inline (recursive field expansion).
        context.Reflect<MaterialComponent>()
            .Type("Material").Custom<ComponentTraitsRuntime>(ComponentTraits<MaterialComponent>{})
            .Data<&MaterialComponent::m_material>("Material").Custom<Spark::MaterialRefElement>(false)
            ;

        Spark::ComponentOperation<MaterialComponent>(context);
    }
}
