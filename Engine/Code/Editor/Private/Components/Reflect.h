#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <ECS/WorldContext.h>
#include "Position.h"

namespace Spark
{
    // Add函数重载多，这里直接将其实例化，否则反射时匹配不到
    template decltype(auto) WorldContext::Add<Editor::Position>(Entity entity, const Editor::Position& component);
}

namespace Editor
{

    static void Reflect(Spark::ReflectContext& context)
    {
        using Spark::WorldContext;
        using Spark::Entity;

        using AddPosition = Position& (WorldContext::*)(Entity entity, const Position& component);
        static constexpr AddPosition AddPosotionFunc = WorldContext::Add;

        context.Reflect<Position>()
            .Type("Position")
            .Data<&Position::x>("x")
            .Data<&Position::y>("y")
            .Data<&Position::z>("z")
            .Func<&WorldContext::Has<Position>>("HasComponent")
            .Func<&WorldContext::Get<Position>>("GetComponent")
            .Func<AddPosotionFunc>("AddComponent")
            .Func<&WorldContext::Remove<Position>>("RemoveComponent")
            .Func<&WorldContext::Repalce<Position>>("ReplaceComponent")
            ;
    }
    
} // namespace Editor
