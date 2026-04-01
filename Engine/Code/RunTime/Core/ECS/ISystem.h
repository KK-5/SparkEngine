#pragma once

#include <shared_mutex>
#include <memory>
#include <utility>
#include <EASTL/vector.h>
#include <EASTL/type_traits.h>
#include <Base.h>
#include <HashString/HashString.h>

namespace Spark
{
    // Base class of all system
    class ISystem
    {
    public:
        ISystem() = default;
        virtual ~ISystem() = default;

        ISystem(const ISystem&) = delete;
        ISystem& operator=(const ISystem&) = delete;

        ISystem(ISystem &&) = default;
        ISystem& operator=(ISystem&&) = default;

        bool IsInitialized() const
        {
            return m_initialized;
        }

        void Init();
        void Shutdown();

        virtual eastl::vector<HashString> Request() const    = 0;
        virtual HashString                GetName() const    = 0;
    
    protected:
        bool m_initialized {false};

    private:
        virtual void                      InitInternal()     = 0;
        virtual void                      ShutdownInternal() = 0;
    };


    struct SystemDeleter
    {
        void operator()(ISystem* p) const noexcept
        {
            if (!p) return;
            if (p->IsInitialized())
            {
                p->Shutdown();
            }
            delete p;
        }
    };

    template<class T>
    using SystemUniquePtr = UniquePtr<T, SystemDeleter>;

    template<class T, class... Args>
    SystemUniquePtr<T> CreateSystem(Args&&... args)
    {
        static_assert(eastl::is_base_of_v<ISystem, T>, "T must derive from ISystem");
        return SystemUniquePtr<T>(
            new T(std::forward<Args>(args)...)
        );
    }
}
