#pragma once

#include <Device/DeviceObject.h>

namespace Spark::Render::RHI
{
    class FrameAttachment;
    class ResourcePool;
    class ResourceView;

    // Init() use DeviceObject::Init
    class Resource : public DeviceObject
    {
        friend class ResourcePool; // for SetPool
    public:
        virtual ~Resource() = default;

        void Shutdown() override final;

        bool IsAttachment() const;

        const ResourcePool* GetPool() const;
        ResourcePool* GetPool();

        const FrameAttachment* GetFrameAttachment() const;

    private:
        void SetFrameAttachment(FrameAttachment* frameAttachment);

        void SetPool(ResourcePool* pool);
                                    
        ResourcePool* m_pool = nullptr;
        FrameAttachment* m_frameAttachment = nullptr;
        bool m_isInvalidationQueued = false;
    };
}