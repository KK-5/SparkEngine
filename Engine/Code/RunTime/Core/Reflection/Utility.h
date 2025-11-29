#pragma once

#include <ECS/WorldContext.h>

namespace Spark
{
    template<typename T>
    void ComponentOpertion(ReflectContext& context)
    {
        context.Reflect<T>()
            .Func<&WorldContext::Has<T>>("HasComponent")
            .Func<&WorldContext::TryGet<T>>("GetComponent")
            .Func<&WorldContext::AddOrRepalce<T, T>>("AddComponent")
            .Func<&WorldContext::Remove<T>>("RemoveComponent")
            .Func<&WorldContext::Repalce<T, T>>("ReplaceComponent");
    }
    ;
}