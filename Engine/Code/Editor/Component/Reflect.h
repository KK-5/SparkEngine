#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <ECS/WorldContext.h>
#include <Serialization/UIElement.h>
#include "Position.h"
#include "AllUIElement.h"

namespace Editor
{

    static void Reflect(Spark::ReflectContext& context)
    {
        using Spark::WorldContext;

        context.Reflect<Position>()
            .Type("Position")
            .Data<&Position::x>("x").Custom<Spark::FloatElement>()
            .Data<&Position::y>("y").Custom<Spark::FloatElement>()
            .Data<&Position::z>("z").Custom<Spark::FloatElement>()
            .Func<&WorldContext::Has<Position>>("HasComponent")
            .Func<&WorldContext::TryGet<Position>>("GetComponent")
            .Func<&WorldContext::AddOrRepalce<Position>>("AddComponent")
            .Func<&WorldContext::Remove<Position>>("RemoveComponent")
            .Func<&WorldContext::Repalce<Position>>("ReplaceComponent")
            ;

        context.Reflect<EnumElem>()
            .Type("EnumElement")
            .Data<EnumElem::One>("One")
            .Data<EnumElem::Two>("Two")
            .Data<EnumElem::Three>("Three");

        using namespace Spark;
        
        context.Reflect<AllUIElement>()
            .Type("AllUIElement")
            .Data<&AllUIElement::editString>("EditString").Custom<EditTextElement>()
            .Data<&AllUIElement::readonlyString>("ReadonlyString").Custom<ReadonlyTextElement>()
            .Data<&AllUIElement::floatElement>("FloatElement").Custom<FloatElement>(0.f, 20.f, 1.f)
            .Data<&AllUIElement::floatSlider>("FloatSlider").Custom<FloatSliderElement>(0.f, 1.f, 0.1f)
            .Data<&AllUIElement::intElement>("IntElement").Custom<IntElement>(0, 20, 1.f)
            .Data<&AllUIElement::intSlider>("IntSlider").Custom<IntSliderElement>(0, 3)
            .Data<&AllUIElement::boolElement>("BoolElement").Custom<BoolElement>()
            .Data<&AllUIElement::vec2Element>("Vec2Element").Custom<Vec2Element>()
            .Data<&AllUIElement::vec3Element>("Vec3Element").Custom<Vec3Element>()
            .Data<&AllUIElement::colorElement>("ColorElement").Custom<ColorElement>()
            .Data<&AllUIElement::enumElement>("EnumElem").Custom<EnumElement>()
            .Func<&WorldContext::Has<AllUIElement>>("HasComponent")
            .Func<&WorldContext::TryGet<AllUIElement>>("GetComponent")
            .Func<&WorldContext::AddOrRepalce<AllUIElement>>("AddComponent")
            .Func<&WorldContext::Remove<AllUIElement>>("RemoveComponent")
            .Func<&WorldContext::Repalce<AllUIElement>>("ReplaceComponent")
            ;
    }
    
} // namespace Editor
