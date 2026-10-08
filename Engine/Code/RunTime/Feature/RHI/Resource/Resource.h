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

        const ResourcePool* GetPool() const;
        ResourcePool* GetPool();

    private:
        void SetPool(ResourcePool* pool);

        ResourcePool* m_pool = nullptr;
        bool m_isInvalidationQueued = false;
    };
}