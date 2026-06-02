#pragma once

#include <EASTL/type_traits.h>
#include <EASTL/meta.h>
#include <Math/Bit.h>

namespace Spark
{
    // Marker type: requesting access to all components.
    struct All
    {
    };

    enum class ComponentAccess : uint32_t
    {
        None = 0,
        Read = BIT(1),
        Write = BIT(2),
        ReadWrite = Read | Write,
    };

    template <typename T, ComponentAccess AccessV = ComponentAccess::None>
    struct ComponentAcquire
    {
        // Strip cv qualifiers so duplicate checks treat const T and T as same component.
        using component_type = eastl::remove_cv_t<T>;
        static constexpr ComponentAccess access = AccessV;
    };

    template <typename T>
    using ReadComponent = ComponentAcquire<T, ComponentAccess::Read>;

    template <typename T>
    using WriteComponent = ComponentAcquire<T, ComponentAccess::Write>;

    template <typename T>
    using ReadWriteComponent = ComponentAcquire<T, ComponentAccess::ReadWrite>;

    template <typename AcquireType>
    struct IsAllComponentAcquire : eastl::bool_constant<eastl::is_same_v<typename AcquireType::component_type, All>>
    {
    };

    template <typename AcquireType>
    inline constexpr bool IsAllComponentAcquireV = IsAllComponentAcquire<AcquireType>::value;

    template <typename... Args>
    struct ComponentAcquireList
    {
        using type = typename eastl::meta::unique_type_list<Args...>::type;
    };

    template <typename... Args>
    using ComponentAcquires = ComponentAcquireList<Args...>;

#define SPARK_COMPONENT_ACCESS(...) \
    public: \
    using ComponentAcquires = Spark::ComponentAcquireList<__VA_ARGS__>;

#define SPARK_SYSTEM_TRAITS(ClassName) \
    using SystemTraits = Spark::SystemTraits<ClassName>; \
    using ContextRef = Spark::WorldContextReference<SystemTraits>;

    template <typename SystemType, typename = void>
    struct SystemTraits
    {
        // Systems that do not declare ComponentAcquires are treated as no component dependency.
        using acquires = eastl::meta::type_list<>;
    };

    template <typename SystemType>
    struct SystemTraits<SystemType, eastl::void_t<typename SystemType::ComponentAcquires>>
    {
        using acquires = typename SystemType::ComponentAcquires::type;
    };
}