#pragma once

#include <EASTL/vector.h>
#include <entt/meta/container.hpp>

//! entt ships meta container traits for the std:: containers only, so a reflected
//! eastl::vector field would not be recognized as a sequence container -- silently, at
//! runtime, with is_sequence_container() simply answering false.
//!
//! ReflectContext.h includes this header so the specialization is visible before any
//! eastl::vector meta node can be instantiated. Making each call site remember to include
//! it would be an ODR violation waiting to happen.
//!
//! eastl::array is deliberately absent: entt decides fixed vs dynamic extent from whether
//! std::tuple_size<Type> is complete, and EASTL specializes eastl::tuple_size instead. An
//! eastl::array would be taken for a dynamic container, and the traits would instantiate
//! the clear()/resize() it does not have.
namespace entt
{
    template<typename... Args>
    struct meta_sequence_container_traits<eastl::vector<Args...>>
        : basic_meta_sequence_container_traits<eastl::vector<Args...>>
    {};
}
