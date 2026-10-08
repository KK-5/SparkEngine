#pragma once

#include <RHI/Device/DeviceObject.h>

namespace Spark::RHI
{
    class ResourcePool;
    class ResourceView;

    class Resource : public DeviceObject
    {
        friend class ResourcePool;  // for SetPool, Init
    public:
        virtual ~Resource();

        void Shutdown() override final;

    private:
        void SetPool(ResourcePool* pool);

        //! The pool it was initialized on. Not handed out: whoever needs the pool holds it,
        //! and asks it whether a resource is its own (ResourcePool::Contains).
        ResourcePool* m_pool = nullptr;
        bool m_isInvalidationQueued = false;
    };
}