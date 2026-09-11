#pragma once

#include "Reflection/ReflectContext.h"
#include "Reflection/Utility.h"
#include "HashString/HashString.h"
#include "Hierarchy/HierarchyComponent.h"
#include "CoreComponents/Name.h"
#include "ECS/WorldContext.h"
#include "Math/Color.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Math/Quaternion.h"
#include "Serialization/EntityJson.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/UIElement.h"
#include "Serialization/MetaFieldTraits.h"

namespace Spark
{
    static void Reflect(ReflectContext& context)
    {
        // Named so diagnostics can say which type refused a value; it is part of the
        // on-disk format now, and an unnamed meta type logs as <unnamed>.
        context.Reflect<Entity>().Type("Entity");
        ReflectJsonOperation<Entity, &EntityToJsonField<Entity>, &EntityFromJsonField<Entity>>(context);


        // Components are marked even where no field uses them yet (Vector2, Quaternion):
        // an unmarked one is still a class, so it would take the object branch, find no
        // serializable field and quietly encode as {}.
        context.Reflect<Math::Vector2>().Type("Vector2")
            .Data<&Math::Vector2::x>("x").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Vector2::y>("y").Traits(MetaFieldTraits::Serializable);

        context.Reflect<Math::Vector3>().Type("Vector3")
            .Data<&Math::Vector3::x>("x").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Vector3::y>("y").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Vector3::z>("z").Traits(MetaFieldTraits::Serializable);

        context.Reflect<Math::Vector4>().Type("Vector4")
            .Data<&Math::Vector4::x>("x").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Vector4::y>("y").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Vector4::z>("z").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Vector4::w>("w").Traits(MetaFieldTraits::Serializable);

        // A colour writes itself r/g/b/a. It derives from Vector4's underlying type but is
        // a type of its own, which is the whole reason it can carry different field names.
        context.Reflect<Math::Color>().Type("Color")
            .Data<&Math::Color::r>("r").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Color::g>("g").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Color::b>("b").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Color::a>("a").Traits(MetaFieldTraits::Serializable);

        context.Reflect<Math::Quaternion>().Type("Quaternion")
            .Data<&Math::Quaternion::x>("x").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Quaternion::y>("y").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Quaternion::z>("z").Traits(MetaFieldTraits::Serializable)
            .Data<&Math::Quaternion::w>("w").Traits(MetaFieldTraits::Serializable);


        context.Reflect<Name>().Type("Name").Traits(ComponentTraits<Name>::flags)
            .Data<&Name::name>("Value").Custom<EditTextElement>()
                .Traits(MetaFieldTraits::Serializable);
            
        ComponentOperation<Name>(context);

        // No ComponentOperation: it would put four editable raw handles in the inspector,
        // an invitation to break the tree -- IHierarchy is how a tree is edited. Without
        // GetComponent the inspector does not list it at all.
        context.Reflect<Hierarchy>().Type("Hierarchy").Traits(ComponentTraits<Hierarchy>::flags)
            .Data<&Hierarchy::parent>("Parent").Traits(MetaFieldTraits::Serializable)
            .Data<&Hierarchy::firstChild>("First Child").Traits(MetaFieldTraits::Serializable)
            .Data<&Hierarchy::prevSibling>("Prev Sibling").Traits(MetaFieldTraits::Serializable)
            .Data<&Hierarchy::nextSibling>("Next Sibling").Traits(MetaFieldTraits::Serializable);
    }
}