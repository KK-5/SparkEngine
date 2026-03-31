#pragma once

#include <RHI/Device/DeviceObject.h>
#include <RHI/Resource/ResourceState.h>

namespace Spark::RHI
{
    class FrameAttachment;
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

        bool IsAttachment() const;

        const ResourcePool* GetPool() const;
        ResourcePool* GetPool();

        const FrameAttachment* GetFrameAttachment() const;

        ResourceState GetResourceState() const;

    private:
        void SetResourceState(ResourceState state);

        void SetFrameAttachment(FrameAttachment* frameAttachment);

        void SetPool(ResourcePool* pool);
                                    
        ResourcePool* m_pool = nullptr;
        FrameAttachment* m_frameAttachment = nullptr;
        ResourceState m_resourceState;
        bool m_isInvalidationQueued = false;
    };
}