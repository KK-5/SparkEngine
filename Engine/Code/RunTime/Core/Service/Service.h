#pragma once

#include <shared_mutex>
#include <memory>

#include <EASTL/vector.h>
#include <HashString/HashString.h>

namespace Spark
{
    /* 
    * 如果一个system需要被其他system使用，使用Service将其注册到全局环境
    * 使用Service管理的system应该是一个工具系统，比如日志系统，它们不需要响应引擎的OnTick事件，可以在
    * 任何时候调用
    * 
    * Service不会对注册实例的生命周期进行管理，所以使用前可能需要检测实例是否存在
    * if (auto system = Service<Isystem>::Get())
    * {
    *     system->DoSomething();
    * }
    * 
    * 注册到Service中的类应该是一个接口类型
    * 
    */
    template<typename T>
    class Service final
    {
    using Pointer = T*;
    public:
        static bool Register(T* instance);

        static bool Unregister(T* instance);

        static T* Get();

        class Handler: public T
        {
        public:
            Handler();
            virtual ~Handler();
        };

    private:
        inline static Pointer& GetInstance()
        {
            static Pointer s_instance;
            return s_instance;
        }
        

        static std::shared_mutex s_mutex;
        static bool s_instanceAssigned;
    };

    template <typename T>
    std::shared_mutex Service<T>::s_mutex{};

    template <typename T>
    bool Service<T>::s_instanceAssigned = false;

    template<typename T>
    bool Service<T>::Register(T* instance)
    {
        if (!instance)
        {
            return false;
        }

        if (Get())
        {
            return false;
        }

        std::unique_lock lock(s_mutex);
        GetInstance() = instance;
        s_instanceAssigned = true;
        return true;
    }

    template<typename T>
    bool Service<T>::Unregister(T* instance)
    {
        if (!s_instanceAssigned)
        {
            return false;
        }

        if(Get() && Get() != instance)
        {
            return false;
        }

        std::unique_lock lock(s_mutex);
        GetInstance() = nullptr;
        s_instanceAssigned = false;
        return true;
    }

    template<typename T>
    T* Service<T>::Get()
    {
        std::shared_lock lock(s_mutex);
        if (s_instanceAssigned)
        {
            return GetInstance();
        }
        return nullptr;
    }

    template <typename T>
    Service<T>::Handler::Handler()
    {
        Service<T>::Register(this);
    }

    template <typename T>
    Service<T>::Handler::~Handler()
    {
        Service<T>::Unregister(this);
    }
}
