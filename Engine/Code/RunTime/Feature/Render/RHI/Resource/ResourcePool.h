#pragma once

#include <mutex>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>
#include <EASTL/unordered_set.h>

#include <Bus/FrameEventBus.h>
#include <Device/DeviceObject.h>

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

        ResourcePoolResolver* GetResolver();
        const ResourcePoolResolver* GetResolver() const;

    protected:
        ResourcePool() = default;

        void OnFrameBegin() override;
        void OnFrameCompileBegin() override;
        void OnFrameEnd() override;

        void SetResolver(eastl::unique_ptr<ResourcePoolResolver>&& resolvePolicy);

        virtual void ShutdownInternal() {}

        virtual void ShutdownResourceInternal(Resource& resource) {}

        // backend
        using BackendMethod = eastl::function<ResultCode()>;

        ResultCode Init(Device& device, const BackendMethod& initMethod);

        ResultCode InitResource(Resource* resource, const BackendMethod& initResourceMethod);

        bool IsRegistered(const Resource* resource) const;

        bool ValidateIsInitialized() const;

        bool ValidateNotProcessingFrame() const;
    
    private:
        void ShutdownResource(Resource* resource);

        void Register(Resource& resource);

        void Unregister(Resource& resource);

        mutable std::shared_mutex m_registryMutex;
        eastl::unordered_set<Resource*> m_registry;
        eastl::unique_ptr<ResourcePoolResolver> m_resolver;
        eastl::atomic<bool> m_isProcessingFrame = false;
    };
}