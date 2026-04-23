#pragma once

#include <RHI/Device/DeviceObject.h>
#include <RHI/Resource/ResourceState.h>

namespace Spark::RHI
{
    class ResourcePool;
    class ResourceView;

    class Resource : public DeviceObject
    {
        friend class ResourcePool;  // for SetPool, Init, SetResourceState
        friend class CommandList;   // for SetResourceState (barrier updates)
        friend class SwapChain;     // for SetResourceState (barrier updates)
    public:
        virtual ~Resource();

        void Shutdown() override final;

        const ResourcePool* GetPool() const;
        ResourcePool* GetPool();

        ResourceState GetResourceState() const;

    private:
        void SetResourceState(ResourceState state);

        void SetPool(ResourcePool* pool);
                                    
        ResourcePool* m_pool = nullptr;
        ResourceState m_resourceState;
        bool m_isInvalidationQueued = false;
    };
}