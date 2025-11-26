#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <ECS/WorldContext.h>
#include <Serialization/UIElement.h>
#include "Position.h"


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
    }
    
} // namespace Editor
