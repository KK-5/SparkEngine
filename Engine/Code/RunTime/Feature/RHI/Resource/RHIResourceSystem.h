#pragma once

#include <ECS/ISystem.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Base.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/BufferPoolDescriptor.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Image/ImagePoolDescriptor.h>

namespace Spark::RHI
{
    //! Frame-begin system that scans RHIContext for imported entities carrying
    //! a resource descriptor (BufferDescriptor / ImageDescriptor / ...) but no
    //! backing RHI object yet, and materializes the underlying RHI resource.
    class RHIResourceSystem final : public ISystem
                                  , public FrameEventBus::Handler
    {
    public:
        RHIResourceSystem() = default;

        // ISystem
        eastl::vector<HashString> Request() const override { return {"RHI"_hs}; }
        HashString GetName() const override { return "RHIResourceSystem"_hs; }

        // FrameEventBus
        void OnFrameBegin() override;

    protected:
        void InitInternal() override;
        void ShutdownInternal() override;

    private:
        void CreateBuffers(RHIContext& ctx, Device& device);
        void CreateImages(RHIContext& ctx, Device& device);
        void CreateBufferViews(RHIContext& ctx, Device& device);
        void CreateImageViews(RHIContext& ctx, Device& device);

        BufferPool* SelectBufferPool(const BufferDescriptor& desc) const;
        ImagePool*  SelectImagePool(const ImageDescriptor& desc) const;

        Ptr<BufferPool> m_devicePlacedBufferPool;
        Ptr<BufferPool> m_deviceCommittedBufferPool;
        Ptr<BufferPool> m_hostUploadPlacedBufferPool;
        Ptr<BufferPool> m_hostReadbackPlacedBufferPool;
        Ptr<ImagePool>  m_deviceImagePool;
        Ptr<ImagePool>  m_hostReadbackImagePool;
    };
}
