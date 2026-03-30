#include "ImageDescriptor.h"

#include <Math/Bit.h>

namespace Spark::RHI
{
    ResourceState GetResourceStateFromImageBindFlags(ImageBindFlags bindFlags)
    {
        const bool renderTarget = CheckBitsAny(bindFlags, ImageBindFlags::Color);
        const bool copyDest = CheckBitsAny(bindFlags, ImageBindFlags::CopyWrite);
        const bool depthTarget = CheckBitsAny(bindFlags, ImageBindFlags::DepthStencil);
        const bool shaderResource = CheckBitsAny(bindFlags, ImageBindFlags::ShaderRead);
        const bool copySource = CheckBitsAny(bindFlags, ImageBindFlags::CopyRead);
        const bool writeState = renderTarget || copyDest || depthTarget;
        const bool readState = shaderResource || copySource;

        if (writeState)
        {
            if (renderTarget)
            {
                return ResourceState{ AttachmentUsage::RenderTarget, AttachmentAccess::Write };
            }
            if (copyDest)
            {
                return ResourceState{ AttachmentUsage::Copy, AttachmentAccess::Write };
            }
            if (depthTarget)
            {
                return ResourceState{ AttachmentUsage::DepthStencil, AttachmentAccess::Write };
            }
        }
        else if (readState)
        {
            if (shaderResource)
            {
                return ResourceState{ AttachmentUsage::Shader, AttachmentAccess::Read };
            }
            if (copySource)
            {
                return ResourceState{ AttachmentUsage::Copy, AttachmentAccess::Read };
            }
        }
        else if (CheckBitsAny(bindFlags, ImageBindFlags::ShaderWrite))
        {
            return ResourceState{ AttachmentUsage::Shader, AttachmentAccess::Write };
        }
        else if (CheckBitsAny(bindFlags, ImageBindFlags::ShadingRate))
        {
            return ResourceState{ AttachmentUsage::ShadingRate, AttachmentAccess::Read };
        }

        return ResourceState{ AttachmentUsage::Uninitialized, AttachmentAccess::Unknown };
    }

    ImageDescriptor ImageDescriptor::Create1D(
        ImageBindFlags bindFlags,
        uint32_t width,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_dimension = ImageDimension::Image1D;
        descriptor.m_size.m_width = width;
        descriptor.m_format = format;
        return descriptor;
    }

    ImageDescriptor ImageDescriptor::Create1DArray(
        ImageBindFlags bindFlags,
        uint32_t width,
        uint16_t arraySize,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_dimension = ImageDimension::Image1D;
        descriptor.m_size.m_width = width;
        descriptor.m_arraySize = arraySize;
        descriptor.m_format = format;
        return descriptor;
    }

    ImageDescriptor ImageDescriptor::Create2D(
        ImageBindFlags bindFlags,
        uint32_t width,
        uint32_t height,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_size.m_width = width;
        descriptor.m_size.m_height = height;
        descriptor.m_format = format;
        return descriptor;
    }

    ImageDescriptor ImageDescriptor::Create2DArray(
        ImageBindFlags bindFlags,
        uint32_t width,
        uint32_t height,
        uint16_t arraySize,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_size.m_width = width;
        descriptor.m_size.m_height = height;
        descriptor.m_arraySize = arraySize;
        descriptor.m_format = format;
        return descriptor;
    }

    ImageDescriptor ImageDescriptor::CreateCubemap(
        ImageBindFlags bindFlags,
        uint32_t width,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_size.m_width = width;
        descriptor.m_size.m_height = width;
        descriptor.m_arraySize = NumCubeMapSlices;
        descriptor.m_format = format;
        descriptor.m_isCubemap = true;
        return descriptor;
    }

    ImageDescriptor ImageDescriptor::CreateCubemapArray(
        ImageBindFlags bindFlags,
        uint32_t width,
        uint16_t arraySize,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_size.m_width = width;
        descriptor.m_size.m_height = width;
        descriptor.m_arraySize = NumCubeMapSlices * arraySize;
        descriptor.m_format = format;
        descriptor.m_isCubemap = true;
        return descriptor;
    }

    ImageDescriptor ImageDescriptor::Create3D(
        ImageBindFlags bindFlags,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        Format format)
    {
        ImageDescriptor descriptor;
        descriptor.m_bindFlags = bindFlags;
        descriptor.m_dimension = ImageDimension::Image3D;
        descriptor.m_size.m_width = width;
        descriptor.m_size.m_height = height;
        descriptor.m_size.m_depth = depth;
        descriptor.m_format = format;
        return descriptor;
    }
}