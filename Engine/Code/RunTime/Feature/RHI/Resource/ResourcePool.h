#pragma once

#include <mutex>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>
#include <EASTL/unordered_set.h>

#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Device/DeviceObject.h>
#include <RHI/Resource/ResourceState.h>

#include "ResourcePoolDescriptor.h"

namespace Spark::RHI
{
    class Resource;

    class ResourcePool : public DeviceObject,
                         public FrameEventBus::Handler
    {
    public:
        virtual ~ResourcePool();

        void Shutdown() override final;

        uint32_t GetResourceCount() const;

        void ShutdownResource(Resource* resource);

        virtual const ResourcePoolDescriptor& GetDescriptor() const = 0;

    protected:
        ResourcePool() = default;

        // FrameEventBus
        void OnFrameBegin() override;
        void OnFrameCompileBegin() override;
        void OnFrameEnd() override;

        ///////////////////////////////////////////////
        // Backend
        virtual void ShutdownInternal() {}
        virtual void ShutdownResourceInternal(Resource& resource) {}
        using BackendMethod = eastl::function<ResultCode()>;
        ResultCode Init(Device& device, [[maybe_unused]] const ResourcePoolDescriptor& descriptor, const BackendMethod& initMethod);
        ResultCode InitResource(Resource* resource, const BackendMethod& initResourceMethod);
        ///////////////////////////////////////////////

        bool ValidateIsUnregistered(const Resource* resource) const;

        bool ValidateIsRegistered(const Resource* resource) const;

        /// Sets the resource state on a resource managed by this pool.
        void SetResourceState(Resource& resource, ResourceState state);

        bool ValidateIsInitialized() const;

        bool ValidateNotProcessingFrame() const;
    
    private:
        void Register(Resource& resource);

        void Unregister(Resource& resource);

        eastl::atomic<bool> m_isProcessingFrame = false;
    };
}