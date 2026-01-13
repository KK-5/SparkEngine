#include "Conversions.h"

#include <Math/Bit.h>
#include <Log/SpdLogSystem.h>

#include "Resource/Buffer/Buffer.h"

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

    void ConvertBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        D3D12_SHADER_RESOURCE_VIEW_DESC& shaderResourceView)
    {
        const uint32_t elementOffsetBase = static_cast<uint32_t>(buffer.GetMemoryView().GetOffset()) / bufferViewDescriptor.m_elementSize;
        const uint32_t elementOffset = elementOffsetBase + bufferViewDescriptor.m_elementOffset;

        if (elementOffsetBase * bufferViewDescriptor.m_elementSize != buffer.GetMemoryView().GetOffset())
        {
            LOG_ERROR("[RHI DX12] ConvertBufferView - SRV: buffer wasn't aligned with element size; buffer should be created"
                " with proper alignment");
        }

        shaderResourceView = {};
        shaderResourceView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (CheckBitsAll(buffer.GetDescriptor().m_bindFlags, RHI::BufferBindFlags::RayTracingAccelerationStructure))
        {
            shaderResourceView.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            shaderResourceView.Format = DXGI_FORMAT_UNKNOWN;
            shaderResourceView.RaytracingAccelerationStructure.Location = buffer.GetMemoryView().GetGpuAddress();
        }
        else
        {
            shaderResourceView.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            shaderResourceView.Format = ConvertFormat(bufferViewDescriptor.m_elementFormat);
            shaderResourceView.Buffer.FirstElement = elementOffset;
            shaderResourceView.Buffer.NumElements = bufferViewDescriptor.m_elementCount;

            if (bufferViewDescriptor.m_elementFormat == RHI::Format::R32_UINT)
            {
                shaderResourceView.Format = DXGI_FORMAT_R32_TYPELESS;
                shaderResourceView.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            }
            else if (shaderResourceView.Format == DXGI_FORMAT_UNKNOWN)
            {
                shaderResourceView.Buffer.StructureByteStride = bufferViewDescriptor.m_elementSize;
            }
        }
    }

    void ConvertBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        D3D12_UNORDERED_ACCESS_VIEW_DESC& unorderedAccessView)
    {
        const uint32_t elementOffsetBase = static_cast<uint32_t>(buffer.GetMemoryView().GetOffset()) / bufferViewDescriptor.m_elementSize;
        const uint32_t elementOffset = elementOffsetBase + bufferViewDescriptor.m_elementOffset;

        if (elementOffsetBase * bufferViewDescriptor.m_elementSize != buffer.GetMemoryView().GetOffset())
        {
            LOG_ERROR("[RHI DX12] ConvertBufferView - UAV: buffer wasn't aligned with element size; buffer should be created"
                " with proper alignment");
        }
        unorderedAccessView = {};
        unorderedAccessView.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        unorderedAccessView.Format = ConvertFormat(bufferViewDescriptor.m_elementFormat);
        unorderedAccessView.Buffer.FirstElement = elementOffset;
        unorderedAccessView.Buffer.NumElements = bufferViewDescriptor.m_elementCount;

        if (bufferViewDescriptor.m_elementFormat == RHI::Format::R32_UINT)
        {
            unorderedAccessView.Format = DXGI_FORMAT_R32_TYPELESS;
            unorderedAccessView.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        }
        else if (unorderedAccessView.Format == DXGI_FORMAT_UNKNOWN)
        {
            unorderedAccessView.Buffer.StructureByteStride = bufferViewDescriptor.m_elementSize;
        }
    }

    void ConvertBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        D3D12_CONSTANT_BUFFER_VIEW_DESC& constantBufferView)
    {
        ASSERT(IsAligned(buffer.GetMemoryView().GetGpuAddress(), Alignment::Constant),
            "Constant Buffer memory is not aligned.");

        const uint32_t bufferOffset = bufferViewDescriptor.m_elementOffset * bufferViewDescriptor.m_elementSize;
        if (!IsAligned(bufferOffset, Alignment::Constant))
        {
            LOG_ERROR("[RHI DX12] Buffer View offset is not aligned to {} bytes, the view won't have the appropiate alignment for Constant Buffer reads.", static_cast<uint32_t>(Alignment::Constant));
        }
        // In DX12 Constant data reads must be a multiple of 256 bytes.
        // It's not a problem if the actual buffer size is smaller since the heap (where the buffer resides) must be multiples of 64KB.
        // This means the buffer view will never go out of heap memory, it might read pass the Constant Buffer size, but it will never be used.
        const uint32_t bufferSize = AlignUp(bufferViewDescriptor.m_elementCount * bufferViewDescriptor.m_elementSize, static_cast<uint32_t>(Alignment::Constant));

        constantBufferView.BufferLocation = buffer.GetMemoryView().GetGpuAddress() + bufferOffset;
        constantBufferView.SizeInBytes = bufferSize;
    }

    D3D12_FILTER_REDUCTION_TYPE ConvertReductionType(RHI::ReductionType reductionType)
    {
        switch (reductionType)
        {
        case RHI::ReductionType::Filter:
            return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        case RHI::ReductionType::Comparison:
            return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
        case RHI::ReductionType::Minimum:
            return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
        case RHI::ReductionType::Maximum:
            return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
        }

        ASSERT(false, "bad conversion in ConvertReductionType");
        return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    }

    D3D12_FILTER_TYPE ConvertFilterMode(RHI::FilterMode mode)
    {
        switch (mode)
        {
        case RHI::FilterMode::Point:
            return D3D12_FILTER_TYPE_POINT;
        case RHI::FilterMode::Linear:
            return D3D12_FILTER_TYPE_LINEAR;
        }

        ASSERT(false, "bad conversion in ConvertFilterMode");
        return D3D12_FILTER_TYPE_POINT;
    }

    D3D12_TEXTURE_ADDRESS_MODE ConvertAddressMode(RHI::AddressMode addressMode)
    {
        switch (addressMode)
        {
        case RHI::AddressMode::Wrap:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHI::AddressMode::Clamp:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHI::AddressMode::Mirror:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHI::AddressMode::MirrorOnce:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        case RHI::AddressMode::Border:
            return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        }

        ASSERT(false, "bad conversion in ConvertAddressMode");
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
    
    void ConvertBorderColor(RHI::BorderColor color, float outputColor[4])
    {
        switch (color)
        {
        case RHI::BorderColor::OpaqueBlack:
            outputColor[0] = outputColor[1] = outputColor[2] = 0.0f;
            outputColor[3] = 1.0f;
            return;
        case RHI::BorderColor::TransparentBlack:
            outputColor[0] = outputColor[1] = outputColor[2] = outputColor[3] = 0.0f;
            return;
        case RHI::BorderColor::OpaqueWhite:
            outputColor[0] = outputColor[1] = outputColor[2] = outputColor[3] = 1.0f;
            return;
        }
    }

    D3D12_COMPARISON_FUNC ConvertComparisonFunc(RHI::ComparisonFunc func)
    {
        static const D3D12_COMPARISON_FUNC table[] =
        {
            D3D12_COMPARISON_FUNC_NEVER,
            D3D12_COMPARISON_FUNC_LESS,
            D3D12_COMPARISON_FUNC_EQUAL,
            D3D12_COMPARISON_FUNC_LESS_EQUAL,
            D3D12_COMPARISON_FUNC_GREATER,
            D3D12_COMPARISON_FUNC_NOT_EQUAL,
            D3D12_COMPARISON_FUNC_GREATER_EQUAL,
            D3D12_COMPARISON_FUNC_ALWAYS
        };
        return table[(uint32_t)func];
    }

    void ConvertSamplerState(const RHI::SamplerState& state, D3D12_SAMPLER_DESC& samplerDesc)
    {
        D3D12_FILTER filter;
        D3D12_FILTER_REDUCTION_TYPE reduction = ConvertReductionType(state.m_reductionType);
        if (state.m_anisotropyEnable)
        {
            filter = D3D12_ENCODE_ANISOTROPIC_FILTER(reduction);
        }
        else
        {
            D3D12_FILTER_TYPE min = ConvertFilterMode(state.m_filterMin);
            D3D12_FILTER_TYPE mag = ConvertFilterMode(state.m_filterMag);
            D3D12_FILTER_TYPE mip = ConvertFilterMode(state.m_filterMip);
            filter = D3D12_ENCODE_BASIC_FILTER(min, mag, mip, reduction);
        }

        samplerDesc.AddressU = ConvertAddressMode(state.m_addressU);
        samplerDesc.AddressV = ConvertAddressMode(state.m_addressV);
        samplerDesc.AddressW = ConvertAddressMode(state.m_addressW);
        ConvertBorderColor(state.m_borderColor, samplerDesc.BorderColor);
        samplerDesc.ComparisonFunc = ConvertComparisonFunc(state.m_comparisonFunc);
        samplerDesc.Filter = filter;
        samplerDesc.MaxAnisotropy = uint8_t(state.m_anisotropyMax);
        samplerDesc.MaxLOD = uint8_t(state.m_mipLodMax);
        samplerDesc.MinLOD = uint8_t(state.m_mipLodMin);
        samplerDesc.MipLODBias = state.m_mipLodBias;
    }
}