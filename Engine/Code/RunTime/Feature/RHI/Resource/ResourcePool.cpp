#include "ResourcePool.h"

#include <Log/SpdLogSystem.h>

#include "Resource.h"

namespace Spark::RHI
{
    ResourcePool::~ResourcePool()
    {
        if (FrameEventBus::Handler::BusIsConnected())
        {
            FrameEventBus::Handler::BusDisconnect();
        }
    }

    bool ResourcePool::ValidateIsUnregistered(const Resource* resource) const
    {
        if (Validation::isEnabled)
        {
            if (!resource || resource->GetPool() != nullptr)
            {
                LOG_ERROR("[ResourcePool] Resource {} is null or is registered on another pool.", GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }

        return true;
    }

    bool ResourcePool::ValidateIsRegistered(const Resource* resource) const
    {
        if (Validation::isEnabled)
        {
            if (!resource || resource->GetPool() != this)
            {
                LOG_ERROR("[ResourcePool] Resource {} is null or is not registered on this pool.", GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }

        return true;
    }

    void ResourcePool::SetResourceState(Resource& resource, ResourceState state)
    {
        resource.SetResourceState(state);
    }

    bool ResourcePool::ValidateIsInitialized() const
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized() == false)
            {
                LOG_ERROR("[ResourcePool] ResourcePool pool is not initialized.");
                return false;
            }
        }
        return true;
    }

    bool ResourcePool::ValidateNotProcessingFrame() const
    {
        if (Validation::isEnabled)
        {
            if (m_isProcessingFrame)
            {
                LOG_ERROR("[ResourcePool] {} Attempting an operation that is invalid when processing the frame.", GetName().GetCStr());
                return false;
            }
        }

        return true;
    }

    void ResourcePool::Register(Resource& resource)
    {
        resource.SetPool(this);
    }

    void ResourcePool::Unregister(Resource& resource)
    {
        resource.SetPool(nullptr);
    }

    ResultCode ResourcePool::Init(Device& device, const ResourcePoolDescriptor& descriptor, const BackendMethod& initMethod)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[ResourcePool] ResourcePool {} is already initialized.", GetName().GetCStr());
                return ResultCode::InvalidOperation;
            }
        }

        ResultCode resultCode = initMethod();
        if (resultCode == ResultCode::Success)
        {
            DeviceObject::Init(device);
            FrameEventBus::Handler::BusConnect();
        }
        return resultCode;
    }

    void ResourcePool::Shutdown()
    {
        if (!ValidateNotProcessingFrame())
        {
            LOG_ERROR("[ResourcePool] {} Attempting to shutdown while processing the frame is undefined behavior.", GetName().GetCStr());
            return;
        }
        // Multiple shutdown is allowed for pools.
        if (IsInitialized())
        {
            FrameEventBus::Handler::BusDisconnect();
            /*
            for (Resource* resource : m_registry)
            {
                resource->SetPool(nullptr);
                ShutdownResourceInternal(*resource);
                resource->Shutdown();
            }
            */
            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }

    ResultCode ResourcePool::InitResource(Resource* resource, const BackendMethod& initResourceMethod)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (!ValidateIsUnregistered(resource))
        {
            return ResultCode::InvalidArgument;
        }

        const ResultCode resultCode = initResourceMethod();
        if (resultCode == ResultCode::Success)
        {
            resource->Init(GetDevice());
            Register(*resource);
        }
        return resultCode;
    }

    void ResourcePool::ShutdownResource(Resource* resource)
    {
        if (ValidateIsInitialized() && ValidateIsRegistered(resource))
        {
            Unregister(*resource);
            ShutdownResourceInternal(*resource);
        }
    }

    void ResourcePool::OnFrameBegin()
    {
    }

    void ResourcePool::OnFrameCompileBegin()
    {
        if (Validation::isEnabled)
        {
            m_isProcessingFrame = true;
        }
    }

    void ResourcePool::OnFrameEnd()
    {
        if (Validation::isEnabled)
        {
            m_isProcessingFrame = false;
        }
    }
}