#pragma once

#include <Object/IObjectFactory.h>
#include <Memory/PoolAllocator.h>
#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    template <typename T>
    class DeviceObjectFactory : public IObjectFactory<T>
    {
    public:
        struct Descriptor : public IObjectFactory<T>::Descriptor
        {

        };

        T* CreateObject()
        {
            AllocateAddress address = m_allocator.allocate(sizeof(T));
            new (address.GetAddress()) T();
            return (T*)address;
        }

        /*
        template <typename... Args>
        T* CreateObject(Args&&... args)
        {
            AllocateAddress address = m_allocator.allocate(sizeof(T));
            new (address.GetAddress()) T(eastl::forward<Args>(args)...);
            return (T*)address;
        }
        */

        void DestoryObject(T* ptr, [[maybe_unused]]bool isPoolShutdown)
        {
            ptr->~T();
            m_allocator.deallocate(ptr);
        }

        void ResetObject(T* object)
        {
            if (object->IsInitialized())
            {
                LOG_ERROR("[DeviceObjectFactory] Reuse the initialized device object!");
            }
        }

        bool IsRecycleObject(T* object)
        {
            (void)object;
            return true;
        }

    private:
        PoolAllocator m_allocator;
    };
}