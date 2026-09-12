#pragma once

#include <entt/entt.hpp>

#include <EASTL/type_traits.h>

#include <HashString/HashString.h>
#include <Log/ILogSystem.h>
#include <Reflection/ReflectContext.h>

#include "ComponentTraits.h"
#include "ContextStorage.h"

namespace Spark
{
    namespace Internal
    {
        //! The one thing about a component that cannot be reached from a TypeId: entt builds a
        //! storage out of the element type, and the type is gone by then.
        template<typename E, typename T>
        typename entt::basic_registry<E>::common_type* ComponentStorageFn(ContextStorage<E>* context)
        {
            return &context->template GetStorage<T>();
        }

        //! Merge rewrites the references a component declares in ComponentTraits; the runtime
        //! path finds them through reflection instead, by field type. The two agree only while
        //! every declared reference is also a reflected field, so say so when they do not.
        template<typename E, typename T>
        void CheckEntityRefMirror(const MetaType& type)
        {
            size_t reflected = 0;
            for (auto&& [id, data] : type.data())
            {
                if (data.type().info() == GetTypeInfo<E>())
                {
                    ++reflected;
                }
            }

            using Refs = eastl::remove_cv_t<decltype(ComponentTraits<T>::entityRefs)>;
            constexpr size_t declared = Refs::template CountOf<E, T>();
            if (declared != reflected)
            {
                LOG_WARN("[ComponentRuntime] {} declares {} entity reference(s) and reflects {}; "
                         "a runtime merge only rewrites the reflected ones.",
                    GetTypeInfo<T>().name(), declared, reflected);
            }
        }
    }

    /// @brief Let a component take part in runtime data -- arriving as bytes from a file or a
    /// script, and merged by a Merge that has no compile-time type list.
    ///
    /// Call it after the type's fields are reflected: the mirror check reads them.
    template<typename T>
    void ComponentRuntime(ReflectContext& context)
    {
        using E = typename ComponentTraits<T>::entity_type;

        // The runtime path copy-constructs: the erased storage takes an opaque element. entt
        // answers a non-copyable one by quietly inserting nothing, so it has to fail here.
        static_assert(eastl::is_copy_constructible<T>::value,
            "A component that cannot be copied cannot come from runtime data.");

        context.Reflect<T>().template Func<&Internal::ComponentStorageFn<E, T>>("ComponentStorage");
        Internal::CheckEntityRefMirror<E, T>(context.Resolve<T>());
    }

    /// @brief The storage a runtime-described component belongs in, created on first use.
    ///
    /// Null when the type never went through ComponentRuntime, or belongs to another context --
    /// the registered function names its own entity type, so a mismatch simply does not match.
    template<typename E>
    typename entt::basic_registry<E>::common_type* RuntimeComponentStorage(
        const MetaType& type, ContextStorage<E>& context)
    {
        using Storage = typename entt::basic_registry<E>::common_type;

        const MetaFunc storageFn = type ? type.func("ComponentStorage"_hs) : MetaFunc{};
        if (!storageFn)
        {
            return nullptr;
        }

        MetaAny result = storageFn.invoke({}, &context);
        Storage* const* storage = result.try_cast<Storage*>();
        return storage != nullptr ? *storage : nullptr;
    }
}
