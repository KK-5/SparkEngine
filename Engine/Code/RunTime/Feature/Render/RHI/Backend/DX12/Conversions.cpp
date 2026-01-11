#include "Conversions.h"

#include <Math/Bit.h>
#include <Resource/Buffer/BufferDescriptor.h>

#include <Log/SpdLogSystem.h>

namespace Spark::RHI::DX12
{
    DXGI_FORMAT ConvertFormat(RHI::Format format, [[maybe_unused]] bool raiseAsserts)
    {
        switch (format)
        {
        case RHI::Format::Unknown:
            return DXGI_FORMAT_UNKNOWN;
        case RHI::Format::R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHI::Format::R32G32B32A32_UINT:
            return DXGI_FORMAT_R32G32B32A32_UINT;
        case RHI::Format::R32G32B32A32_SINT:
            return DXGI_FORMAT_R32G32B32A32_SINT;
        case RHI::Format::R32G32B32_FLOAT:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHI::Format::R32G32B32_UINT:
            return DXGI_FORMAT_R32G32B32_UINT;
        case RHI::Format::R32G32B32_SINT:
            return DXGI_FORMAT_R32G32B32_SINT;
        case RHI::Format::R16G16B16A16_FLOAT:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHI::Format::R16G16B16A16_UNORM:
            return DXGI_FORMAT_R16G16B16A16_UNORM;
        case RHI::Format::R16G16B16A16_UINT:
            return DXGI_FORMAT_R16G16B16A16_UINT;
        case RHI::Format::R16G16B16A16_SNORM:
            return DXGI_FORMAT_R16G16B16A16_SNORM;
        case RHI::Format::R16G16B16A16_SINT:
            return DXGI_FORMAT_R16G16B16A16_SINT;
        case RHI::Format::R32G32_FLOAT:
            return DXGI_FORMAT_R32G32_FLOAT;
        case RHI::Format::R32G32_UINT:
            return DXGI_FORMAT_R32G32_UINT;
        case RHI::Format::R32G32_SINT:
            return DXGI_FORMAT_R32G32_SINT;
        case RHI::Format::D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case RHI::Format::R10G10B10A2_UNORM:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case RHI::Format::R10G10B10A2_UINT:
            return DXGI_FORMAT_R10G10B10A2_UINT;
        case RHI::Format::R11G11B10_FLOAT:
            return DXGI_FORMAT_R11G11B10_FLOAT;
        case RHI::Format::R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHI::Format::R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHI::Format::R8G8B8A8_UINT:
            return DXGI_FORMAT_R8G8B8A8_UINT;
        case RHI::Format::R8G8B8A8_SNORM:
            return DXGI_FORMAT_R8G8B8A8_SNORM;
        case RHI::Format::R8G8B8A8_SINT:
            return DXGI_FORMAT_R8G8B8A8_SINT;
        case RHI::Format::R16G16_FLOAT:
            return DXGI_FORMAT_R16G16_FLOAT;
        case RHI::Format::R16G16_UNORM:
            return DXGI_FORMAT_R16G16_UNORM;
        case RHI::Format::R16G16_UINT:
            return DXGI_FORMAT_R16G16_UINT;
        case RHI::Format::R16G16_SNORM:
            return DXGI_FORMAT_R16G16_SNORM;
        case RHI::Format::R16G16_SINT:
            return DXGI_FORMAT_R16G16_SINT;
        case RHI::Format::D32_FLOAT:
            return DXGI_FORMAT_D32_FLOAT;
        case RHI::Format::R32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case RHI::Format::R32_UINT:
            return DXGI_FORMAT_R32_UINT;
        case RHI::Format::R32_SINT:
            return DXGI_FORMAT_R32_SINT;
        case RHI::Format::D24_UNORM_S8_UINT:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHI::Format::R8G8_UNORM:
            return DXGI_FORMAT_R8G8_UNORM;
        case RHI::Format::R8G8_UINT:
            return DXGI_FORMAT_R8G8_UINT;
        case RHI::Format::R8G8_SNORM:
            return DXGI_FORMAT_R8G8_SNORM;
        case RHI::Format::R8G8_SINT:
            return DXGI_FORMAT_R8G8_SINT;
        case RHI::Format::R16_FLOAT:
            return DXGI_FORMAT_R16_FLOAT;
        case RHI::Format::D16_UNORM:
            return DXGI_FORMAT_D16_UNORM;
        case RHI::Format::R16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
        case RHI::Format::R16_UINT:
            return DXGI_FORMAT_R16_UINT;
        case RHI::Format::R16_SNORM:
            return DXGI_FORMAT_R16_SNORM;
        case RHI::Format::R16_SINT:
            return DXGI_FORMAT_R16_SINT;
        case RHI::Format::R8_UNORM:
            return DXGI_FORMAT_R8_UNORM;
        case RHI::Format::R8_UINT:
            return DXGI_FORMAT_R8_UINT;
        case RHI::Format::R8_SNORM:
            return DXGI_FORMAT_R8_SNORM;
        case RHI::Format::R8_SINT:
            return DXGI_FORMAT_R8_SINT;
        case RHI::Format::A8_UNORM:
            return DXGI_FORMAT_A8_UNORM;
        case RHI::Format::R1_UNORM:
            return DXGI_FORMAT_R1_UNORM;
        case RHI::Format::R9G9B9E5_SHAREDEXP:
            return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case RHI::Format::R8G8_B8G8_UNORM:
            return DXGI_FORMAT_R8G8_B8G8_UNORM;
        case RHI::Format::G8R8_G8B8_UNORM:
            return DXGI_FORMAT_G8R8_G8B8_UNORM;
        case RHI::Format::BC1_UNORM:
            return DXGI_FORMAT_BC1_UNORM;
        case RHI::Format::BC1_UNORM_SRGB:
            return DXGI_FORMAT_BC1_UNORM_SRGB;
        case RHI::Format::BC2_UNORM:
            return DXGI_FORMAT_BC2_UNORM;
        case RHI::Format::BC2_UNORM_SRGB:
            return DXGI_FORMAT_BC2_UNORM_SRGB;
        case RHI::Format::BC3_UNORM:
            return DXGI_FORMAT_BC3_UNORM;
        case RHI::Format::BC3_UNORM_SRGB:
            return DXGI_FORMAT_BC3_UNORM_SRGB;
        case RHI::Format::BC4_UNORM:
            return DXGI_FORMAT_BC4_UNORM;
        case RHI::Format::BC4_SNORM:
            return DXGI_FORMAT_BC4_SNORM;
        case RHI::Format::BC5_UNORM:
            return DXGI_FORMAT_BC5_UNORM;
        case RHI::Format::BC5_SNORM:
            return DXGI_FORMAT_BC5_SNORM;
        case RHI::Format::B5G6R5_UNORM:
            return DXGI_FORMAT_B5G6R5_UNORM;
        case RHI::Format::B5G5R5A1_UNORM:
            return DXGI_FORMAT_B5G5R5A1_UNORM;
        case RHI::Format::B8G8R8A8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHI::Format::B8G8R8X8_UNORM:
            return DXGI_FORMAT_B8G8R8X8_UNORM;
        case RHI::Format::R10G10B10_XR_BIAS_A2_UNORM:
            return DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM;
        case RHI::Format::B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHI::Format::B8G8R8X8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        case RHI::Format::BC6H_UF16:
            return DXGI_FORMAT_BC6H_UF16;
        case RHI::Format::BC6H_SF16:
            return DXGI_FORMAT_BC6H_SF16;
        case RHI::Format::BC7_UNORM:
            return DXGI_FORMAT_BC7_UNORM;
        case RHI::Format::BC7_UNORM_SRGB:
            return DXGI_FORMAT_BC7_UNORM_SRGB;
        case RHI::Format::AYUV:
            return DXGI_FORMAT_AYUV;
        case RHI::Format::Y410:
            return DXGI_FORMAT_Y410;
        case RHI::Format::Y416:
            return DXGI_FORMAT_Y416;
        case RHI::Format::NV12:
            return DXGI_FORMAT_NV12;
        case RHI::Format::P010:
            return DXGI_FORMAT_P010;
        case RHI::Format::P016:
            return DXGI_FORMAT_P016;
        case RHI::Format::YUY2:
            return DXGI_FORMAT_YUY2;
        case RHI::Format::Y210:
            return DXGI_FORMAT_Y210;
        case RHI::Format::Y216:
            return DXGI_FORMAT_Y216;
        case RHI::Format::NV11:
            return DXGI_FORMAT_NV11;
        case RHI::Format::AI44:
            return DXGI_FORMAT_AI44;
        case RHI::Format::IA44:
            return DXGI_FORMAT_IA44;
        case RHI::Format::P8:
            return DXGI_FORMAT_P8;
        case RHI::Format::A8P8:
            return DXGI_FORMAT_A8P8;
        case RHI::Format::B4G4R4A4_UNORM:
            return DXGI_FORMAT_B4G4R4A4_UNORM;
        case RHI::Format::P208:
            return DXGI_FORMAT_P208;
        case RHI::Format::V208:
            return DXGI_FORMAT_V208;
        case RHI::Format::V408:
            return DXGI_FORMAT_V408;

        default:
            ASSERT(!raiseAsserts, "unhandled conversion in ConvertFormat");
            return DXGI_FORMAT_UNKNOWN;
        }
    }

    D3D12_RESOURCE_FLAGS ConvertBufferBindFlags(RHI::BufferBindFlags bufferFlags)
    {
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        if (CheckBitsAll(bufferFlags, RHI::BufferBindFlags::ShaderWrite))
        {
            resourceFlags = SetBits(resourceFlags, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        }
        if (CheckBitsAny(bufferFlags, RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::RayTracingAccelerationStructure))
        {
            resourceFlags = ResetBits(resourceFlags, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        }
        return resourceFlags;
    }

    void ConvertBufferDescriptor(const RHI::BufferDescriptor& descriptor, D3D12_RESOURCE_DESC& resourceDesc)
    {
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = descriptor.m_alignment;
        resourceDesc.Width = descriptor.m_byteCount;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = ConvertBufferBindFlags(descriptor.m_bindFlags);
    }

    D3D12_RESOURCE_DIMENSION ConvertImageDimension(RHI::ImageDimension dimension)
    {
        switch (dimension)
        {
        case RHI::ImageDimension::Image1D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case RHI::ImageDimension::Image2D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        case RHI::ImageDimension::Image3D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE3D;

        default:
            ASSERT(false, "failed to convert image type");
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        }
    }

    D3D12_RESOURCE_FLAGS ConvertImageBindFlags(RHI::ImageBindFlags imageFlags)
    {
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        if (CheckBitsAll(imageFlags, RHI::ImageBindFlags::ShaderWrite))
        {
            resourceFlags = SetBits(resourceFlags, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        }
        if (CheckBitsAll(imageFlags, RHI::ImageBindFlags::ShaderRead))
        {
            resourceFlags = ResetBits(resourceFlags, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        }
        if (CheckBitsAll(imageFlags, RHI::ImageBindFlags::Color))
        {
            resourceFlags = SetBits(resourceFlags, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        }
        if (CheckBitsAny(imageFlags, RHI::ImageBindFlags::DepthStencil))
        {
            resourceFlags = SetBits(resourceFlags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        }
        else
        {
            resourceFlags = ResetBits(resourceFlags, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        }
        return resourceFlags;
    }

    void ConvertImageDescriptor(const RHI::ImageDescriptor& descriptor, D3D12_RESOURCE_DESC& resourceDesc)
    {
        resourceDesc.Dimension = ConvertImageDimension(descriptor.m_dimension);
        resourceDesc.Alignment = 0;
        resourceDesc.Width = descriptor.m_size.m_width;
        resourceDesc.Height = descriptor.m_size.m_height;
        resourceDesc.DepthOrArraySize = static_cast<uint16_t>(resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? descriptor.m_size.m_depth : descriptor.m_arraySize);
        resourceDesc.MipLevels = descriptor.m_mipLevels;
        resourceDesc.Format = GetBaseFormat(ConvertFormat(descriptor.m_format));
        resourceDesc.SampleDesc = DXGI_SAMPLE_DESC{ descriptor.m_multisampleState.m_samples, descriptor.m_multisampleState.m_quality };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = ConvertImageBindFlags(descriptor.m_bindFlags);
    }

    D3D12_CLEAR_VALUE ConvertClearValue(RHI::Format format, RHI::ClearValue clearValue)
    {
        switch (clearValue.m_type)
        {
        case RHI::ClearValueType::DepthStencil:
            return CD3DX12_CLEAR_VALUE(ConvertFormat(format), clearValue.m_depthStencil.m_depth, clearValue.m_depthStencil.m_stencil);;
        case RHI::ClearValueType::Vector4Float:
        {
            float color[] =
            {
                clearValue.m_vector4Float[0],
                clearValue.m_vector4Float[1],
                clearValue.m_vector4Float[2],
                clearValue.m_vector4Float[3]
            };
            return CD3DX12_CLEAR_VALUE(ConvertFormat(format), color);
        }
        case RHI::ClearValueType::Vector4Uint:
            ASSERT(false, "Can't convert unsigned type to DX12 clear value. Use float instead.");
            return CD3DX12_CLEAR_VALUE{};
        }
        return CD3DX12_CLEAR_VALUE{};
    }

    D3D12_HEAP_TYPE ConvertHeapType(RHI::HeapMemoryLevel heapMemoryLevel, RHI::HostMemoryAccess hostMemoryAccess)
    {
        switch (heapMemoryLevel)
        {
        case RHI::HeapMemoryLevel::Host:
            switch (hostMemoryAccess)
            {
            case RHI::HostMemoryAccess::Write:
                return D3D12_HEAP_TYPE_UPLOAD;
            case RHI::HostMemoryAccess::Read:
                return D3D12_HEAP_TYPE_READBACK;
            };
        case RHI::HeapMemoryLevel::Device:
            return D3D12_HEAP_TYPE_DEFAULT;
        }
        ASSERT(false, "Invalid Heap Type");
        return D3D12_HEAP_TYPE_CUSTOM;
    }

    D3D12_RESOURCE_STATES ConvertInitialResourceState(RHI::HeapMemoryLevel heapMemoryLevel, RHI::HostMemoryAccess hostMemoryAccess)
    {
        if (heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            return hostMemoryAccess == RHI::HostMemoryAccess::Write ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    }
}