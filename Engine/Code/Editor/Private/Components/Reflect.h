#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <ECS/WorldContext.h>
#include "Position.h"


namespace Editor
{

    static void Reflect(Spark::ReflectContext& context)
    {
        using Spark::WorldContext;

        context.Reflect<Position>()
            .Type("Position")
            .Data<&Position::x>("x")
            .Data<&Position::y>("y")
            .Data<&Position::z>("z")
            .Func<&WorldContext::Has<Position>>("HasComponent")
            .Func<&WorldContext::TryGet<Position>>("GetComponent")
            .Func<&WorldContext::AddOrRepalce<Position>>("AddComponent")
            .Func<&WorldContext::Remove<Position>>("RemoveComponent")
            .Func<&WorldContext::Repalce<Position>>("ReplaceComponent")
            ;
    }
    
} // namespace Editor
