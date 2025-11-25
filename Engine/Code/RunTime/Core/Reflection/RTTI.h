#pragma once

#include <entt/meta/meta.hpp>
#include <entt/core/type_info.hpp>

namespace Spark
{
    using MetaType = entt::meta_type;
    using MetaAny = entt::meta_any;
    using TypeInfo = entt::type_info;
    using MetaData = entt::meta_data;
    using MetaFunc = entt::meta_func;
    using MetaSquenceContainer = entt::meta_sequence_container;
    using TypeId = entt::id_type;
    using MetaCustom = entt::meta_custom;

    template<typename T>
    MetaAny AnyCast(T&& value) 
    {
        return entt::forward_as_meta(std::forward<T>(value));
    }

    template<typename T>
    static constexpr TypeInfo GetTypeInfo()
    {
       return entt::type_id<T>();
    }

    template<typename T>
    static constexpr TypeId GetTypeId() {
        return entt::type_hash<T>::value();
    }

}