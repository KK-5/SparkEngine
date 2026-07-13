#include "ResourceState.h"

#include <Math/Bit.h>
#include <Log/ILogSystem.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferBindFlags.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageEnums.h>

namespace Spark::RHI
{
    namespace
    {
        // Each requested access bit must be permitted by the resource's creation
        // bind flags. A bit that is not requested imposes no constraint.
        bool IsBufferAccessSupported(const Buffer& buffer, AccessFlags access)
        {
            const BufferBindFlags bindFlags = buffer.GetDescriptor().m_bindFlags;
            auto req = [&](AccessFlags bit, BufferBindFlags need)
            {
                return !CheckBitsAny(access, bit) || CheckBitsAny(bindFlags, need);
            };

            return req(AccessFlags::TransferRead,       BufferBindFlags::CopyRead)
                && req(AccessFlags::TransferWrite,      BufferBindFlags::CopyWrite)
                && req(AccessFlags::ShaderSampledRead,  BufferBindFlags::ShaderRead | BufferBindFlags::RayTracingShaderTable)
                && req(AccessFlags::ConstantBufferRead, BufferBindFlags::Constant)
                && req(AccessFlags::ShaderStorageRead,  BufferBindFlags::ShaderWrite | BufferBindFlags::RayTracingScratchBuffer)
                && req(AccessFlags::ShaderStorageWrite, BufferBindFlags::ShaderWrite | BufferBindFlags::RayTracingScratchBuffer)
                && req(AccessFlags::VertexIndexInput,   BufferBindFlags::InputAssembly | BufferBindFlags::DynamicInputAssembly)
                && req(AccessFlags::IndirectRead,       BufferBindFlags::Indirect)
                && req(AccessFlags::PredicationRead,    BufferBindFlags::Predication)
                && req(AccessFlags::AccelStructRead,    BufferBindFlags::RayTracingAccelerationStructure)
                && req(AccessFlags::AccelStructWrite,   BufferBindFlags::RayTracingAccelerationStructure);
        }

        bool IsImageAccessSupported(const Image& image, AccessFlags access)
        {
            // Swap-chain present imposes no bind-flag constraint.
            if (CheckBitsAny(access, AccessFlags::Present))
            {
                return true;
            }

            const ImageBindFlags bindFlags = image.GetDescriptor().m_bindFlags;
            auto req = [&](AccessFlags bit, ImageBindFlags need)
            {
                return !CheckBitsAny(access, bit) || CheckBitsAny(bindFlags, need);
            };

            return req(AccessFlags::ColorAttachmentWrite, ImageBindFlags::Color)
                && req(AccessFlags::ColorAttachmentRead,  ImageBindFlags::Color)
                && req(AccessFlags::DepthStencilRead,     ImageBindFlags::DepthStencil)
                && req(AccessFlags::DepthStencilWrite,    ImageBindFlags::DepthStencil)
                && req(AccessFlags::ShaderSampledRead,    ImageBindFlags::ShaderRead)
                && req(AccessFlags::ShaderStorageRead,    ImageBindFlags::ShaderWrite)
                && req(AccessFlags::ShaderStorageWrite,   ImageBindFlags::ShaderWrite)
                && req(AccessFlags::TransferRead,         ImageBindFlags::CopyRead)
                && req(AccessFlags::TransferWrite,        ImageBindFlags::CopyWrite)
                && req(AccessFlags::ResolveRead,          ImageBindFlags::Color)
                && req(AccessFlags::ResolveWrite,         ImageBindFlags::Color)
                && req(AccessFlags::ShadingRateRead,      ImageBindFlags::ShadingRate)
                && req(AccessFlags::InputAttachmentRead,  ImageBindFlags::ShaderRead);
        }
    }

    BufferBarrier MakeBufferBarrier(
        Buffer& buffer,
        AccessFlags dstAccess,
        AttachmentStage dstStage)
    {
        const ResourceState srcState = buffer.GetResourceState();
        BufferBarrier barrier;
        barrier.m_buffer    = &buffer;
        barrier.m_srcAccess = srcState.m_access;
        barrier.m_dstAccess = dstAccess;
        barrier.m_srcStage  = srcState.m_stage;
        barrier.m_dstStage  = dstStage;
        barrier.m_srcQueue  = srcState.m_queue;
        // m_dstQueue defaults to BufferBarrier struct default (Graphics);
        // callers crossing queues override it after construction.

        if (!ValidateBufferBarrier(barrier))
        {
            ASSERT(false, "[RHI] MakeBufferBarrier created an invalid barrier.");
        }
        return barrier;
    }

    ImageBarrier MakeImageBarrier(
        Image& image,
        AccessFlags dstAccess,
        AttachmentStage dstStage)
    {
        const ResourceState srcState = image.GetResourceState();
        ImageBarrier barrier;
        barrier.m_image     = &image;
        barrier.m_srcAccess = srcState.m_access;
        barrier.m_dstAccess = dstAccess;
        barrier.m_srcStage  = srcState.m_stage;
        barrier.m_dstStage  = dstStage;
        barrier.m_srcQueue  = srcState.m_queue;
        // See MakeBufferBarrier above for m_dstQueue rationale.

        if (!ValidateImageBarrier(barrier))
        {
            ASSERT(false, "[RHI] MakeImageBarrier created an invalid barrier.");
        }
        return barrier;
    }

    namespace
    {
        DeviceMemoryBarrier MakeDeviceMemoryBarrierImpl(
            Resource*           before,
            Resource*           after,
            BarrierResourceType typeBefore,
            BarrierResourceType typeAfter,
            AttachmentStage     srcStage,
            AttachmentStage     dstStage)
        {
            ASSERT(after != nullptr, "[RHI] MakeDeviceMemoryBarrier: 'after' resource must be non-null.");
            DeviceMemoryBarrier barrier;
            barrier.m_resourceBefore = before;
            barrier.m_resourceAfter  = after;
            barrier.m_typeBefore     = typeBefore;
            barrier.m_typeAfter      = typeAfter;
            barrier.m_srcStage       = srcStage;
            barrier.m_dstStage       = dstStage;
            return barrier;
        }
    }

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Buffer* before, Buffer* after, AttachmentStage srcStage, AttachmentStage dstStage)
    {
        return MakeDeviceMemoryBarrierImpl(
            before, after, BarrierResourceType::Buffer, BarrierResourceType::Buffer, srcStage, dstStage);
    }

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Buffer* before, Image* after, AttachmentStage srcStage, AttachmentStage dstStage)
    {
        return MakeDeviceMemoryBarrierImpl(
            before, after, BarrierResourceType::Buffer, BarrierResourceType::Image, srcStage, dstStage);
    }

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Image* before, Buffer* after, AttachmentStage srcStage, AttachmentStage dstStage)
    {
        return MakeDeviceMemoryBarrierImpl(
            before, after, BarrierResourceType::Image, BarrierResourceType::Buffer, srcStage, dstStage);
    }

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Image* before, Image* after, AttachmentStage srcStage, AttachmentStage dstStage)
    {
        return MakeDeviceMemoryBarrierImpl(
            before, after, BarrierResourceType::Image, BarrierResourceType::Image, srcStage, dstStage);
    }

    BufferBarrier ConvertToCopyRead(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::TransferRead);
    }

    BufferBarrier ConvertToCopyWrite(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::TransferWrite);
    }

    BufferBarrier ConvertToShaderRead(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::ConstantBufferRead | AccessFlags::ShaderSampledRead);
    }

    BufferBarrier ConvertToShaderWrite(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::ShaderStorageWrite);
    }

    BufferBarrier ConvertToShaderReadWrite(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::ShaderStorageRead | AccessFlags::ShaderStorageWrite);
    }

    BufferBarrier ConvertToInputAssembly(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::VertexIndexInput);
    }

    BufferBarrier ConvertToIndirect(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::IndirectRead);
    }

    BufferBarrier ConvertToPredication(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::PredicationRead);
    }

    BufferBarrier ConvertToRayTracingAccelerationStructure(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AccessFlags::AccelStructRead);
    }

    ImageBarrier ConvertToRenderTarget(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::ColorAttachmentWrite);
    }

    ImageBarrier ConvertToDepthStencilRead(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::DepthStencilRead);
    }

    ImageBarrier ConvertToDepthStencilWrite(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::DepthStencilWrite);
    }

    ImageBarrier ConvertToImageShaderRead(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::ShaderSampledRead);
    }

    ImageBarrier ConvertToImageShaderWrite(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::ShaderStorageWrite);
    }

    ImageBarrier ConvertToImageShaderReadWrite(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::ShaderStorageRead | AccessFlags::ShaderStorageWrite);
    }

    ImageBarrier ConvertToImageCopyRead(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::TransferRead);
    }

    ImageBarrier ConvertToImageCopyWrite(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::TransferWrite);
    }

    ImageBarrier ConvertToShadingRate(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::ShadingRateRead);
    }

    ImageBarrier ConvertToPresent(Image& image)
    {
        return MakeImageBarrier(image, AccessFlags::Present);
    }

    namespace
    {
        //! Cross-queue ownership transfer requires the resource to be visible
        //! on both src and dst queues -- i.e. the descriptor's sharedQueueMask
        //! must cover both bits. Returns true for intra-queue barriers.
        bool IsCrossQueueAllowed(
            HardwareQueueClassMask sharedMask,
            HardwareQueueClass     srcQueue,
            HardwareQueueClass     dstQueue)
        {
            if (srcQueue == dstQueue)
            {
                return true;
            }
            const HardwareQueueClassMask required =
                GetHardwareQueueClassMask(srcQueue) | GetHardwareQueueClassMask(dstQueue);
            return CheckBitsAll(sharedMask, required);
        }
    }

    bool ValidateBufferBarrier(const BufferBarrier& barrier)
    {
        if (!barrier.m_buffer)
        {
            LOG_ERROR("[RHI] ValidateBufferBarrier: null buffer pointer.");
            return false;
        }

        const Buffer& buffer = *barrier.m_buffer;
        if (!IsBufferAccessSupported(buffer, barrier.m_dstAccess))
        {
            LOG_ERROR(
                "[RHI] Invalid buffer barrier for '{}': bindFlags=0x{:x}, requested dstAccess=0x{:x}.",
                buffer.GetName().GetCStr(),
                static_cast<uint32_t>(buffer.GetDescriptor().m_bindFlags),
                static_cast<uint32_t>(barrier.m_dstAccess));
            return false;
        }

        if (!IsCrossQueueAllowed(buffer.GetDescriptor().m_sharedQueueMask, barrier.m_srcQueue, barrier.m_dstQueue))
        {
            LOG_ERROR(
                "[RHI] Cross-queue buffer barrier for '{}' is invalid: sharedQueueMask=0x{:x} does not cover "
                "both srcQueue={} and dstQueue={}.",
                buffer.GetName().GetCStr(),
                static_cast<uint32_t>(buffer.GetDescriptor().m_sharedQueueMask),
                static_cast<uint32_t>(barrier.m_srcQueue),
                static_cast<uint32_t>(barrier.m_dstQueue));
            return false;
        }
        return true;
    }

    bool ValidateImageBarrier(const ImageBarrier& barrier)
    {
        if (!barrier.m_image)
        {
            LOG_ERROR("[RHI] ValidateImageBarrier: null image pointer.");
            return false;
        }

        const Image& image = *barrier.m_image;
        if (!IsImageAccessSupported(image, barrier.m_dstAccess))
        {
            LOG_ERROR(
                "[RHI] Invalid image barrier for '{}': bindFlags=0x{:x}, requested dstAccess=0x{:x}.",
                image.GetName().GetCStr(),
                static_cast<uint32_t>(image.GetDescriptor().m_bindFlags),
                static_cast<uint32_t>(barrier.m_dstAccess));
            return false;
        }

        if (!IsCrossQueueAllowed(image.GetDescriptor().m_sharedQueueMask, barrier.m_srcQueue, barrier.m_dstQueue))
        {
            LOG_ERROR(
                "[RHI] Cross-queue image barrier for '{}' is invalid: sharedQueueMask=0x{:x} does not cover "
                "both srcQueue={} and dstQueue={}.",
                image.GetName().GetCStr(),
                static_cast<uint32_t>(image.GetDescriptor().m_sharedQueueMask),
                static_cast<uint32_t>(barrier.m_srcQueue),
                static_cast<uint32_t>(barrier.m_dstQueue));
            return false;
        }
        return true;
    }
}
