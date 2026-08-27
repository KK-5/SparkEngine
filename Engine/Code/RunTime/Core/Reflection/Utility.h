#pragma once

#include <ECS/WorldContext.h>
#include <ECS/Common.h>

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
     * @return Pointer to the member.
     */
    template<typename Func>
    [[nodiscard]] constexpr auto Overload(Func *func) noexcept {
        return func;
    }

    namespace ReflectDetail
    {
        // Context-free component-op adapters. Each resolves its OWNING context via
        // CtxExec::Current() internally, so the editor never receives or holds a concrete
        // context — it addresses ECS state purely through (reflected type, entity). The
        // entity is passed as a raw uint32 to erase the handle type; the adapter casts it
        // back to E (the context's entity type, supplied explicitly at registration).
        template<typename CtxExec, typename E, typename T>
        bool CompHas(uint32_t e)
        {
            auto* c = CtxExec::Current();
            return c && c->Valid(static_cast<E>(e)) && c->template Has<T>(static_cast<E>(e));
        }

        template<typename CtxExec, typename E, typename T>
        T* CompGet(uint32_t e)
        {
            auto* c = CtxExec::Current();
            return (c && c->Valid(static_cast<E>(e))) ? c->template TryGet<T>(static_cast<E>(e)) : nullptr;
        }

        template<typename CtxExec, typename E, typename T>
        T& CompAdd(uint32_t e, const T& v)
        {
            return CtxExec::Current()->template Add<T, const T&>(static_cast<E>(e), v);
        }

        template<typename CtxExec, typename E, typename T>
        T& CompAddOrReplace(uint32_t e, const T& v)
        {
            return CtxExec::Current()->template AddOrReplace<T, const T&>(static_cast<E>(e), v);
        }

        template<typename CtxExec, typename E, typename T>
        void CompRemove(uint32_t e)
        {
            CtxExec::Current()->template Remove<T>(static_cast<E>(e));
        }

        template<typename CtxExec, typename E, typename T>
        void CompReplace(uint32_t e, const T& v)
        {
            CtxExec::Current()->template Replace<T, const T&>(static_cast<E>(e), v);
        }

        template<bool World>
        bool IsWorldComponentFn() { return World; }
    }

    // Registers the component-operation reflection for component T living in the context
    // reached via CtxExec (thread-local accessor), whose entity type is E. The ops are
    // context-free at the reflection boundary — the editor invokes them with just
    // (entity[, value]) and never touches a context. IsWorld tags whether the component
    // sits directly on world entities (so the inspector lists it at top level) or is
    // reached indirectly (rendered inline, e.g. MaterialParams).
    template<typename CtxExec, typename E, typename T, bool IsWorld = false>
    void ComponentOperation(ReflectContext& context)
    {
        context.Reflect<T>()
            .Func<&ReflectDetail::CompHas<CtxExec, E, T>>("HasComponent")
            .Func<&ReflectDetail::CompGet<CtxExec, E, T>>("GetComponent")
            .Func<&ReflectDetail::CompAdd<CtxExec, E, T>>("AddComponent")
            .Func<&ReflectDetail::CompAddOrReplace<CtxExec, E, T>>("AddOrReplaceComponent")
            .Func<&ReflectDetail::CompRemove<CtxExec, E, T>>("RemoveComponent")
            .Func<&ReflectDetail::CompReplace<CtxExec, E, T>>("ReplaceComponent")
            .Func<&ReflectDetail::IsWorldComponentFn<IsWorld>>("IsWorldComponent");
    }

    // Shorthand for a plain world component. Overloads the form above rather than
    // colliding with it: CtxExec/E/T there are neither defaulted nor deducible from the
    // sole ReflectContext& argument, so an explicit ComponentOperation<T> matches only this.
    template<typename T>
    void ComponentOperation(ReflectContext& context)
    {
        ComponentOperation<WorldExecuteContext, Entity, T, true>(context);
    }
}
