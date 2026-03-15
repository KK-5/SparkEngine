#include "ImageDescriptor.h"

namespace Spark::RHI
{
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