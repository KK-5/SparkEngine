#pragma once

#include <mutex>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>
#include <EASTL/unordered_set.h>

#include <Bus/FrameEventBus.h>
#include <Device/DeviceObject.h>

#include "ResourcePoolDescriptor.h"

namespace Spark::Render::RHI
{
    class ResourcePoolResolver
    {
    public:
        virtual ~ResourcePoolResolver() = default;
    };

    class Resource;

    class ResourcePool : public DeviceObject,
                         public FrameEventBus::Handler
    {
    public:
        virtual ~ResourcePool();

        void Shutdown() override final;

        uint32_t GetResourceCount() const;

        void ShutdownResource(Resource* resource);

        ResourcePoolResolver* GetResolver();
        const ResourcePoolResolver* GetResolver() const;

        virtual const ResourcePoolDescriptor& GetDescriptor() const = 0;

    protected:
        ResourcePool() = default;

        // FrameEventBus
        void OnFrameBegin() override;
        void OnFrameCompileBegin() override;
        void OnFrameEnd() override;

        void SetResolver(eastl::unique_ptr<ResourcePoolResolver>&& resolvePolicy);

        virtual void ShutdownInternal() {}

        virtual void ShutdownResourceInternal(Resource& resource) {}

        // backend
        using BackendMethod = eastl::function<ResultCode()>;
        ResultCode Init(Device& device, [[maybe_unused]] const ResourcePoolDescriptor& descriptor, const BackendMethod& initMethod);
        ResultCode InitResource(Resource* resource, const BackendMethod& initResourceMethod);
        ///////////////////////////////////////////////

        bool IsRegistered(const Resource* resource) const;

        bool ValidateIsInitialized() const;

        bool ValidateNotProcessingFrame() const;
    
    private:
        void Register(Resource& resource);

        void Unregister(Resource& resource);

        mutable std::shared_mutex m_registryMutex;
        eastl::unordered_set<Resource*> m_registry;
        eastl::unique_ptr<ResourcePoolResolver> m_resolver;
        eastl::atomic<bool> m_isProcessingFrame = false;
    };
}