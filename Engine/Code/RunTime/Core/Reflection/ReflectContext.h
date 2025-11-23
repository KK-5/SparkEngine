#pragma once

#include <entt/meta/container.hpp>
#include <entt/meta/context.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/pointer.hpp>
#include <entt/meta/resolve.hpp>
#include <entt/meta/template.hpp>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>
#include <EASTL/sort.h>
#include <EASTL/functional.h>

#include <HashString/HashString.h>

#include "RTTI.h"

namespace Spark
{

    template<typename T>
    class Reflector
    {
        using EnTTReflector = entt::meta_factory<T>;
        using Context = entt::meta_ctx;
    public:
        // entt::meta_factory 会修改 entt::meta_ctx（其中有储存使用的容器），这里需要使用引用
        Reflector(Context& conetxt): m_reflector(EnTTReflector(conetxt)){}

        Reflector Type(const char* name)
        {
            m_reflector.type(name);
            return *this;
        }

        Reflector Type(TypeId id, const char* name)
        {
            m_reflector.type(id, name);
            return *this;
        }

        template<auto DataT>
        Reflector Data(const char* name)
        {
            m_reflector.data<DataT>(name);
            return *this;
        }

        template<auto DataT>
        Reflector Data(TypeId id, const char* name)
        {
            m_reflector.data<DataT>(id, name);
            return *this;
        }

        template<auto FuncT>
        Reflector Func(const char* name)
        {
            m_reflector.func<FuncT>(name);
            return *this;
        }

        template<auto FuncT>
        Reflector Func(TypeId id, const char* name)
        {
            m_reflector.func<FuncT>(id, name);
            return *this;
        }

        template<auto BaseType>
        Reflector Base()
        {
            m_reflector.base<BaseType>();
            return *this;
        }

    private:
        EnTTReflector m_reflector;
    };


    static bool DefaultTypeCompare(const MetaType& first, const MetaType& second)
    {
        return first.id() < second.id();
    }


    class ReflectContext
    {
        using Context = entt::meta_ctx;
        using InternalContext = entt::internal::meta_context;
    public:
        ReflectContext() = default;

        virtual ~ReflectContext()
        {
            entt::meta_reset(m_context);
        }

        void Reset()
        {
            entt::meta_reset(m_context);
        }

        void Reset(TypeId id)
        {
            entt::meta_reset(m_context, id);
        }

        template <typename T>
        void Reset()
        {
            entt::meta_reset<T>(m_context);
        }
        
        template<typename T>
        Reflector<T> Reflect()
        {
            return Reflector<T>(m_context);
        }

        template<typename T>
        MetaType Resolve()
        {
            return entt::resolve<T>(m_context);
        }

        MetaType Resolve(TypeInfo info)
        {
            return entt::resolve(m_context, info);
        }

        MetaType Resolve(TypeId id)
        {
            return entt::resolve(m_context, id);
        }

        size_t TypeSize() const
        {
            InternalContext internalContext = InternalContext::from(m_context);
            return internalContext.value.size();
        }

        eastl::vector<MetaType> GetAllTypes(eastl::function<bool(const MetaType&, const MetaType&)> compare = DefaultTypeCompare) const
        {
            eastl::vector<MetaType> result;
            InternalContext internalContext = InternalContext::from(m_context);
            result.reserve(internalContext.value.size());
            for (const auto& elem: internalContext.value)
            {
                result.emplace_back(m_context, elem.second);
            }
            eastl::sort(result.begin(), result.end(), compare);
            return result;
        }

    private:
        Context m_context {};
    };
}