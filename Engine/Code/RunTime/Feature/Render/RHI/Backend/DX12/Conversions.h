#pragma once

#include <Format.h>
#include <Resource/Buffer/BufferBindFlags.h>
#include <Resource/Buffer/BufferDescriptor.h>
#include <Resource/Buffer/BufferViewDescriptor.h>
#include <Resource/Image/ImageDescriptor.h>
#include <Resource/Image/ImageEnums.h>
#include <ClearValue.h>
#include <MemoryEnums.h>

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

    void ConvertImageDescriptor(const RHI::ImageDescriptor& descriptor, D3D12_RESOURCE_DESC& resourceDesc);

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
}