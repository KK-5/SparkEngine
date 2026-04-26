#include "ResourceState.h"

#include <Math/Bit.h>
#include <Log/SpdLogSystem.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferBindFlags.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageEnums.h>

namespace Spark::RHI
{
    namespace
    {
        bool IsBufferStateSupportedByBindFlags(const Buffer& buffer, AttachmentUsage usage, AttachmentAccess access)
        {
            const BufferBindFlags bindFlags = buffer.GetDescriptor().m_bindFlags;
            const bool wantsRead = CheckBitsAny(access, AttachmentAccess::Read);
            const bool wantsWrite = CheckBitsAny(access, AttachmentAccess::Write);

            switch (usage)
            {
            case AttachmentUsage::Uninitialized:
                return true;
            case AttachmentUsage::Copy:
                if (wantsWrite)
                {
                    return CheckBitsAny(bindFlags, BufferBindFlags::CopyWrite);
                }
                if (wantsRead)
                {
                    return CheckBitsAny(bindFlags, BufferBindFlags::CopyRead);
                }
                return false;
            case AttachmentUsage::Shader:
                if (wantsRead && wantsWrite)
                {
                    return CheckBitsAll(bindFlags, BufferBindFlags::ShaderReadWrite);
                }
                if (wantsWrite)
                {
                    return CheckBitsAny(bindFlags, BufferBindFlags::ShaderWrite | BufferBindFlags::RayTracingScratchBuffer);
                }
                if (wantsRead)
                {
                    return CheckBitsAny(bindFlags,
                        BufferBindFlags::ShaderRead
                        | BufferBindFlags::Constant
                        | BufferBindFlags::RayTracingShaderTable);
                }
                return false;
            case AttachmentUsage::InputAssembly:
                return wantsRead && !wantsWrite
                    && CheckBitsAny(bindFlags, BufferBindFlags::InputAssembly | BufferBindFlags::DynamicInputAssembly);
            case AttachmentUsage::Indirect:
                return wantsRead && !wantsWrite
                    && CheckBitsAny(bindFlags, BufferBindFlags::Indirect);
            case AttachmentUsage::Predication:
                return wantsRead && !wantsWrite
                    && CheckBitsAny(bindFlags, BufferBindFlags::Predication);
            case AttachmentUsage::RayTracingAccelerationStructure:
                return wantsRead && !wantsWrite
                    && CheckBitsAny(bindFlags, BufferBindFlags::RayTracingAccelerationStructure);
            default:
                return false;
            }
        }

        bool IsImageStateSupportedByBindFlags(const Image& image, AttachmentUsage usage, AttachmentAccess access)
        {
            const ImageBindFlags bindFlags = image.GetDescriptor().m_bindFlags;
            const bool wantsRead = CheckBitsAny(access, AttachmentAccess::Read);
            const bool wantsWrite = CheckBitsAny(access, AttachmentAccess::Write);

            switch (usage)
            {
            case AttachmentUsage::Uninitialized:
                return true;
            case AttachmentUsage::RenderTarget:
                return wantsWrite && CheckBitsAny(bindFlags, ImageBindFlags::Color);
            case AttachmentUsage::DepthStencil:
                if (wantsWrite)
                {
                    return CheckBitsAny(bindFlags, ImageBindFlags::DepthStencil);
                }
                if (wantsRead)
                {
                    return CheckBitsAny(bindFlags, ImageBindFlags::DepthStencil);
                }
                return false;
            case AttachmentUsage::Shader:
                if (wantsRead && wantsWrite)
                {
                    return CheckBitsAll(bindFlags, ImageBindFlags::ShaderReadWrite);
                }
                if (wantsWrite)
                {
                    return CheckBitsAny(bindFlags, ImageBindFlags::ShaderWrite);
                }
                if (wantsRead)
                {
                    return CheckBitsAny(bindFlags, ImageBindFlags::ShaderRead);
                }
                return false;
            case AttachmentUsage::Copy:
                if (wantsWrite)
                {
                    return CheckBitsAny(bindFlags, ImageBindFlags::CopyWrite);
                }
                if (wantsRead)
                {
                    return CheckBitsAny(bindFlags, ImageBindFlags::CopyRead);
                }
                return false;
            case AttachmentUsage::ShadingRate:
                return wantsRead && !wantsWrite
                    && CheckBitsAny(bindFlags, ImageBindFlags::ShadingRate);
            case AttachmentUsage::Present:
                return true;
            case AttachmentUsage::Resolve:
                return CheckBitsAny(bindFlags, ImageBindFlags::Color);
            default:
                return false;
            }
        }
    }

    BufferBarrier MakeBufferBarrier(
        Buffer& buffer,
        AttachmentUsage dstUsage,
        AttachmentAccess dstAccess,
        AttachmentStage srcStage,
        AttachmentStage dstStage)
    {
        const ResourceState srcState = buffer.GetResourceState();
        BufferBarrier barrier;
        barrier.m_buffer = &buffer;
        barrier.m_srcUsage = srcState.m_usage;
        barrier.m_dstUsage = dstUsage;
        barrier.m_srcAccess = srcState.m_access;
        barrier.m_dstAccess = dstAccess;
        barrier.m_srcStage = srcStage;
        barrier.m_dstStage = dstStage;

        if (!ValidateBufferBarrier(barrier))
        {
            ASSERT(false, "[RHI] MakeBufferBarrier created an invalid barrier.");
        }
        return barrier;
    }

    ImageBarrier MakeImageBarrier(
        Image& image,
        AttachmentUsage newUsage,
        AttachmentAccess dstAccess,
        AttachmentStage srcStage,
        AttachmentStage dstStage)
    {
        const ResourceState srcState = image.GetResourceState();
        ImageBarrier barrier;
        barrier.m_image = &image;
        barrier.m_srcUsage = srcState.m_usage;
        barrier.m_dstUsage = newUsage;
        barrier.m_srcAccess = srcState.m_access;
        barrier.m_dstAccess = dstAccess;
        barrier.m_srcStage = srcStage;
        barrier.m_dstStage = dstStage;

        if (!ValidateImageBarrier(barrier))
        {
            ASSERT(false, "[RHI] MakeImageBarrier created an invalid barrier.");
        }
        return barrier;
    }

    BufferBarrier ConvertToCopyRead(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Copy, AttachmentAccess::Read);
    }

    BufferBarrier ConvertToCopyWrite(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Copy, AttachmentAccess::Write);
    }

    BufferBarrier ConvertToShaderRead(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Shader, AttachmentAccess::Read);
    }

    BufferBarrier ConvertToShaderWrite(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Shader, AttachmentAccess::Write);
    }

    BufferBarrier ConvertToShaderReadWrite(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Shader, AttachmentAccess::ReadWrite);
    }

    BufferBarrier ConvertToInputAssembly(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::InputAssembly, AttachmentAccess::Read);
    }

    BufferBarrier ConvertToIndirect(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Indirect, AttachmentAccess::Read);
    }

    BufferBarrier ConvertToPredication(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::Predication, AttachmentAccess::Read);
    }

    BufferBarrier ConvertToRayTracingAccelerationStructure(Buffer& buffer)
    {
        return MakeBufferBarrier(buffer, AttachmentUsage::RayTracingAccelerationStructure, AttachmentAccess::Read);
    }

    ImageBarrier ConvertToRenderTarget(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::RenderTarget, AttachmentAccess::Write);
    }

    ImageBarrier ConvertToDepthStencilRead(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::DepthStencil, AttachmentAccess::Read);
    }

    ImageBarrier ConvertToDepthStencilWrite(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::DepthStencil, AttachmentAccess::Write);
    }

    ImageBarrier ConvertToImageShaderRead(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::Shader, AttachmentAccess::Read);
    }

    ImageBarrier ConvertToImageShaderWrite(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::Shader, AttachmentAccess::Write);
    }

    ImageBarrier ConvertToImageShaderReadWrite(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::Shader, AttachmentAccess::ReadWrite);
    }

    ImageBarrier ConvertToImageCopyRead(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::Copy, AttachmentAccess::Read);
    }

    ImageBarrier ConvertToImageCopyWrite(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::Copy, AttachmentAccess::Write);
    }

    ImageBarrier ConvertToShadingRate(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::ShadingRate, AttachmentAccess::Read);
    }

    ImageBarrier ConvertToPresent(Image& image)
    {
        return MakeImageBarrier(image, AttachmentUsage::Present, AttachmentAccess::Read);
    }

    bool ValidateBufferBarrier(const BufferBarrier& barrier)
    {
        if (!barrier.m_buffer)
        {
            LOG_ERROR("[RHI] ValidateBufferBarrier: null buffer pointer.");
            return false;
        }

        const Buffer& buffer = *barrier.m_buffer;
        if (!IsBufferStateSupportedByBindFlags(buffer, barrier.m_dstUsage, barrier.m_dstAccess))
        {
            LOG_ERROR(
                "[RHI] Invalid buffer barrier for '{}': bindFlags={}, requested dstState=(usage={}, access={}).",
                buffer.GetName().GetCStr(),
                static_cast<uint32_t>(buffer.GetDescriptor().m_bindFlags),
                static_cast<uint32_t>(barrier.m_dstUsage),
                static_cast<uint32_t>(barrier.m_dstAccess));
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
        if (!IsImageStateSupportedByBindFlags(image, barrier.m_dstUsage, barrier.m_dstAccess))
        {
            LOG_ERROR(
                "[RHI] Invalid image barrier for '{}': bindFlags={}, requested dstState=(usage={}, access={}).",
                image.GetName().GetCStr(),
                static_cast<uint32_t>(image.GetDescriptor().m_bindFlags),
                static_cast<uint32_t>(barrier.m_dstUsage),
                static_cast<uint32_t>(barrier.m_dstAccess));
            return false;
        }
        return true;
    }
}
