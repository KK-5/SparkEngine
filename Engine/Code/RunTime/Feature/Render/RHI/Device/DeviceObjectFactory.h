#pragma once

#include <Object/IObjectFactory.h>
#include <Memory/PoolAllocator.h>

#include "DeviceObject.h"

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

        void DestoryObject(T* ptr, [[maybe_unused]]bool isPoolShutdown)
        {
            ptr->~T();
            m_allocator.deallocate(ptr);
        }

        void ResetObject(T* object)
        {
            if (object->IsInitialized())
            {
                object->Shutdown();
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