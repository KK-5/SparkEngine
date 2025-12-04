#pragma once

#include <Device/DeviceObject.h>

namespace Spark::Render::RHI
{
    class FrameAttachment;
    class ResourcePool;
    class ResourceView;

    class Resource : public DeviceObject
    {
        friend class ResourcePool; // for SetPool
    public:
        virtual ~Resource() = default;

        bool IsAttachment() const;

        void Shutdown() override final;

        const ResourcePool* GetPool() const;
        ResourcePool* GetPool();

        const FrameAttachment* GetFrameAttachment() const;

        void EraseResourceView(ResourceView* resourceView) const;

    private:
        void SetFrameAttachment(FrameAttachment* frameAttachment);

        void SetPool(ResourcePool* pool);
                                    
        ResourcePool* m_pool = nullptr;

        FrameAttachment* m_frameAttachment = nullptr;

        bool m_isInvalidationQueued = false;
    };
}