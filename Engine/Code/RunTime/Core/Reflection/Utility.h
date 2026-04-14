#pragma once

#include <ECS/WorldContext.h>

namespace Spark
{
    /**
     * @brief Constant utility to disambiguate overloaded members of a class.
     * @tparam Type Type of the desired overload.
     * @tparam Class Type of class to which the member belongs.
     * @param member A valid pointer to a member.
     * @return Pointer to the member.
     */
    template<typename Type, typename Class>
    [[nodiscard]] constexpr auto Overload(Type Class::*member) noexcept {
        return member;
    }

    /**
     * @brief Constant utility to disambiguate overloaded functions.
     * @tparam Func Function type of the desired overload.
     * @param func A valid pointer to a function.
     * @return Pointer to the function.
     */
    template<typename Func>
    [[nodiscard]] constexpr auto Overload(Func *func) noexcept {
        return func;
    }

    template<typename T>
    void ComponentOpertion(ReflectContext& context)
    {
        context.Reflect<T>()
            .Func<&WorldContext::Has<T>>("HasComponent")
            .Func<Overload<T*(Entity)>(&WorldContext::TryGet<T>)>("GetComponent")
            .Func<Overload<T&(Entity, const T&)>(&WorldContext::Add<T, const T&>)>("AddComponent")
            .Func<&WorldContext::AddOrRepalce<T, const T&>>("AddOrReplaceComponent")
            .Func<&WorldContext::Remove<T>>("RemoveComponent")
            .Func<&WorldContext::Repalce<T>>("ReplaceComponent");
    }
    ;
}