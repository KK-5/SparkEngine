#pragma once

#include <RHI/Format.h>
#include <RHI/Resource/Buffer/BufferBindFlags.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageEnums.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>
#include <RHI/Resource/ShaderResource/InputStreamLayout.h>
#include <RHI/Pipeline/ShaderStages.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/ClearValue.h>
#include <RHI/MemoryEnums.h>

#include "DX12.h"

namespace Spark::RHI::DX12
{
    class Buffer;
    class Image;

    DXGI_FORMAT ConvertFormat(RHI::Format format, bool raiseAsserts = true);

    D3D12_RESOURCE_FLAGS ConvertBufferBindFlags(RHI::BufferBindFlags bufferFlags);

    void ConvertBufferDescriptor(const RHI::BufferDescriptor& descriptor, D3D12_RESOURCE_DESC& resourceDesc);

    D3D12_RESOURCE_DIMENSION ConvertImageDimension(RHI::ImageDimension dimension);

    D3D12_RESOURCE_FLAGS ConvertImageBindFlags(RHI::ImageBindFlags imageFlags);

    D3D12_DESCRIPTOR_RANGE_TYPE ConvertShaderInputBufferAccess(RHI::ShaderInputBufferAccess access);

    D3D12_DESCRIPTOR_RANGE_TYPE ConvertShaderInputImageAccess(RHI::ShaderInputImageAccess access);

    D3D12_SRV_DIMENSION ConvertSRVDimension(RHI::ShaderInputImageType type);

    D3D12_UAV_DIMENSION ConvertUAVDimension(RHI::ShaderInputImageType type);

    void ConvertImageDescriptor(const RHI::ImageDescriptor& descriptor, D3D12_RESOURCE_DESC& resourceDesc);

    DXGI_FORMAT ConvertImageViewFormat(const Image& image, const RHI::ImageViewDescriptor& imageViewDescriptor);

    uint16_t ConvertImageAspectToPlaneSlice(RHI::ImageAspect aspect);

    RHI::ImageAspectFlags ConvertPlaneSliceToImageAspectFlags(uint16_t planeSlice);

    D3D12_CLEAR_VALUE ConvertClearValue(RHI::Format format, RHI::ClearValue clearValue);

    D3D12_HEAP_TYPE ConvertHeapType(RHI::HeapMemoryLevel heapMemoryLevel, RHI::HostMemoryAccess hostMemoryAccess);

    D3D12_RESOURCE_STATES ConvertInitialResourceState(RHI::HeapMemoryLevel heapMemoryLevel, RHI::HostMemoryAccess hostMemoryAccess);

    void ConvertBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        D3D12_SHADER_RESOURCE_VIEW_DESC& shaderResourceView);

    void ConvertBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        D3D12_UNORDERED_ACCESS_VIEW_DESC& unorderedAccessView);

    void ConvertBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        D3D12_CONSTANT_BUFFER_VIEW_DESC& constantBufferView);

    void ConvertImageView(
        const Image& image,
        const RHI::ImageViewDescriptor& imageViewDescriptor,
        D3D12_SHADER_RESOURCE_VIEW_DESC& shaderResourceView);

    void ConvertImageView(
        const Image& image,
        const RHI::ImageViewDescriptor& imageViewDescriptor,
        D3D12_UNORDERED_ACCESS_VIEW_DESC& unorderedAccessView);

    void ConvertImageView(
        const Image& image,
        const RHI::ImageViewDescriptor& imageViewDescriptor,
        D3D12_RENDER_TARGET_VIEW_DESC& renderTargetView);

    void ConvertImageView(
        const Image& image,
        const RHI::ImageViewDescriptor& imageViewDescriptor,
        D3D12_DEPTH_STENCIL_VIEW_DESC& depthStencilView);

    D3D12_FILTER_REDUCTION_TYPE ConvertReductionType(RHI::ReductionType reductionType);

    D3D12_FILTER_TYPE ConvertFilterMode(RHI::FilterMode mode);

    D3D12_TEXTURE_ADDRESS_MODE ConvertAddressMode(RHI::AddressMode addressMode);

    D3D12_COMPARISON_FUNC ConvertComparisonFunc(RHI::ComparisonFunc func);
    
    void ConvertSamplerState(const RHI::SamplerState& state, D3D12_SAMPLER_DESC& samplerDesc);

    D3D12_SHADER_VISIBILITY ConvertShaderStageMask(RHI::ShaderStageMask mask);

    void ConvertStaticSampler(
        const RHI::SamplerState& state,
        uint32_t shaderRegister,
        uint32_t shaderRegisterSpace,
        D3D12_SHADER_VISIBILITY shaderVisibility,
        D3D12_STATIC_SAMPLER_DESC& staticSamplerDesc);

    eastl::vector<D3D12_INPUT_ELEMENT_DESC> ConvertInputElements(const RHI::InputStreamLayout& layout);

    D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertToTopologyType(RHI::PrimitiveTopology type);

    D3D12_BLEND ConvertBlendFactor(RHI::BlendFactor factor);

    D3D12_BLEND_OP ConvertBlendOp(RHI::BlendOp op);

    uint8_t ConvertColorWriteMask(uint8_t writeMask);

    D3D12_BLEND_DESC ConvertBlendState(const RHI::BlendState& blend);

    D3D12_CULL_MODE ConvertCullMode(RHI::CullMode mode);

    D3D12_FILL_MODE ConvertFillMode(RHI::FillMode mode);

    D3D12_RASTERIZER_DESC ConvertRasterState(const RHI::RasterState& raster);

    D3D12_STENCIL_OP ConvertStencilOp(RHI::StencilOp op);

    D3D12_DEPTH_WRITE_MASK ConvertDepthWriteMask(RHI::DepthWriteMask mask);

    D3D12_DEPTH_STENCIL_DESC ConvertDepthStencilState(const RHI::DepthStencilState& depthStencil);

    DXGI_SCALING ConvertScaling(RHI::Scaling scaling);
}