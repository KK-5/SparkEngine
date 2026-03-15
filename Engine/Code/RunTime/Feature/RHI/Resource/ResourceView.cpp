#include "ResourceView.h"

#include "Resource.h"

namespace Spark::RHI
{
    ResultCode ResourceView::Init(const Resource& resource)
    {
        RHI::Device& device = resource.GetDevice();

        m_resource = &resource;
        ResultCode resultCode = InitInternal(device, resource);
        if (resultCode != ResultCode::Success)
        {
            m_resource = nullptr;
            return resultCode;
        }

        DeviceObject::Init(device);
        return ResultCode::Success;
    }

    void ResourceView::Shutdown()
    {
        if (IsInitialized())
        {
            ShutdownInternal();

            m_resource = nullptr;
            DeviceObject::Shutdown();
        }
    }

    const Resource& ResourceView::GetResource() const
    {
        return *m_resource;
    }
}