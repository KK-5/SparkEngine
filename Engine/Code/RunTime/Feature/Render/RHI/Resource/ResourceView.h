#pragma once

#include <Device/DeviceObject.h>

namespace Spark::Render::RHI
{
    class Resource;

    class ResourceView : public DeviceObject
    {
    public:
        virtual ~ResourceView() = default;

        const Resource& GetResource() const;

        //bool IsStale() const;

        virtual bool IsFullView() const = 0;
    
    protected:
        // derived class should call ResourceView::Init
        ResultCode Init(const Resource& resource);

    private:
        // DeviceObject overrides
        void Shutdown() override final;

        // backend
        virtual ResultCode InitInternal(Device& device, const Resource& resource) = 0;
        virtual void ShutdownInternal() = 0;
        //virtual ResultCode InvalidateInternal() = 0;
        ///////////////////////////////////////////////

        const Resource* m_resource = nullptr;
    };
}