#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include <Resource/Material/Reflect.h>

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

        // The other half of what a material entity carries, bound the same way and for the
        // same reason: the material window edits these three and writing a `.smat` reads
        // them. Also IsWorld=false — state belongs to a material, never to an object.
        Spark::ComponentOperation<MaterialExecuteContext, MaterialHandle, Resource::MaterialState>(context);

        // Which asset a material entity came from. Reflected so the editor reads it through
        // the same (type, entity) addressing as everything else instead of reaching for a
        // MaterialContext, and because stage 4 writes it into the scene file as a component
        // of the material context. "Asset" is on-disk format once that lands.
        //
        // Read-only in the inspector: retargeting a material entity at another asset is not
        // an edit that makes sense -- pointing an OBJECT at another material is, and that is
        // the world-side MaterialComponent below.
        context.Reflect<MaterialAssetRef>()
            .Type("MaterialAssetRef")
            .Data<&MaterialAssetRef::m_id>("Asset")
                .Custom<Spark::AssetElement>(true, static_cast<uint32_t>(Resource::AssetType::Material))
                .Traits(MetaFieldTraits::Serializable);

        Spark::ComponentOperation<MaterialExecuteContext, MaterialHandle, MaterialAssetRef>(context);

        // The world-side reference held by a primitive entity. A normal editable
        // world component; its single MaterialHandle field uses MaterialRefElement so
        // the editor follows the reference into the MaterialContext and renders the
        // referenced StandardPBR inline (recursive field expansion).
        context.Reflect<MaterialComponent>()
            .Type("Material").Custom<ComponentTraitsRuntime>(ComponentTraits<MaterialComponent>{})
            .Data<&MaterialComponent::m_material>("Material").Custom<Spark::MaterialRefElement>(false)
            ;

        Spark::ComponentOperation<MaterialComponent>(context);

        // Same field layout as StandardPBR, registered on this type because entt's field
        // range does not visit a base. The name breaks the "class name minus Component"
        // key rule on purpose (see TODO_AssetSystemPlan.md's frozen rule) — it reads as
        // two words in the inspector.
        Resource::ReflectStandardPBRFields<StandardPBROverride>(context, "StandardPBR Override");
        context.Reflect<StandardPBROverride>()
            .Custom<ComponentTraitsRuntime>(ComponentTraits<StandardPBROverride>{});

        Spark::ComponentOperation<StandardPBROverride>(context);
    }
}
