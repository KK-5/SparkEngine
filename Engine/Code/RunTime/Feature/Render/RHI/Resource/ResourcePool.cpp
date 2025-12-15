#include "ResourcePool.h"

#include <Log/SpdLogSystem.h>

#include "Resource.h"

namespace Spark::Render::RHI
{
    ResourcePool::~ResourcePool()
    {
        if (ObjectPool::GetObjectCount() != 0)
        {
            LOG_ERROR("[ResourcePool] ResourcePool {} is being destroyed while it still has {} registered resources.", GetName().GetCStr(), static_cast<uint32_t>(m_registry.size()));
        }
    }

    bool ResourcePool::IsInitialized() const
    {
        return DeviceObject::IsInitialized() && ObjectPool::IsInitialized();
    }

    ResourcePoolResolver* ResourcePool::GetResolver()
    {
        return m_resolver.get();
    }

    const ResourcePoolResolver* ResourcePool::GetResolver() const
    {
        return m_resolver.get();
    }

    uint32_t ResourcePool::GetResourceCount() const
    {
        std::shared_lock<std::shared_mutex> lock(m_registryMutex);
        return static_cast<uint32_t>(m_registry.size());
    }

    void ResourcePool::SetResolver(eastl::unique_ptr<ResourcePoolResolver>&& resolver)
    {
        if (!IsInitialized())
        {
            LOG_ERROR("[ResourcePool] Assigning a resolver after the pool has been initialized is not allowed.");
            return;
        }
        m_resolver = eastl::move(resolver);
    }

    bool ResourcePool::IsRegistered(const Resource* resource) const
    {
        if (Validation::isEnabled)
        {
            if (!resource || resource->GetPool() != this)
            {
                LOG_ERROR("[ResourcePool] Resource {} is not registered on this pool.", GetName().GetCStr());
                return false;
            }
        }

        return true;
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

        //std::unique_lock<std::shared_mutex> lock(m_registryMutex);
        //m_registry.emplace(&resource);
    }

    void ResourcePool::Unregister(Resource& resource)
    {
        resource.SetPool(nullptr);

        //std::unique_lock<std::shared_mutex> lock(m_registryMutex);
        //m_registry.erase(&resource);
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

        ObjectPool::Init(descriptor);

        ResultCode resultCode = initMethod();
        if (resultCode == ResultCode::Success)
        {
            DeviceObject::Init(device);
            FrameEventBus::Handler::BusConnect();
            //device.GetResourcePoolDatabase().AttachPool(this);
        }
        return resultCode;
    }

    void ResourcePool::Shutdown()
    {
        if (ValidateNotProcessingFrame())
        {
            LOG_ERROR("[ResourcePool] {} Attempting to shutdown while processing the frame is undefined behavior.", GetName().GetCStr());
            return;
        }
        // Multiple shutdown is allowed for pools.
        if (IsInitialized())
        {
            //GetDevice().GetResourcePoolDatabase().DetachPool(this);
            FrameEventBus::Handler::BusDisconnect();
            for (Resource* resource : m_registry)
            {
                resource->SetPool(nullptr);
                ShutdownResourceInternal(*resource);
                resource->Shutdown();
            }
            ShutdownInternal();
            m_registry.clear();
            m_resolver.reset();
            DeviceObject::Shutdown();
            ObjectPool::Shutdown();
        }
    }

    ResultCode ResourcePool::InitResource(Resource* resource, const BackendMethod& initResourceMethod)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (IsRegistered(resource))
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
        if (ValidateIsInitialized() && IsRegistered(resource))
        {
            Unregister(*resource);
            ShutdownResourceInternal(*resource);
            ObjectPool::ShutdownObject(resource);
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
        ObjectPool::Collect();
    }
}