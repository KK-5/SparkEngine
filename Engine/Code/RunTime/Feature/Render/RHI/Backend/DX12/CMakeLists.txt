set(TARGET_NAME SparkRHI_DX12)

set(SOURCES
    3rdParty/D3D12MA/D3D12MemAlloc.cpp
    DX12.cpp
    MemoryView.cpp
    Conversions.cpp
    Command/CommandListBase.cpp
    Command/CommandList.cpp
    Command/CommandListPool.cpp
    Command/CommandQueue.cpp
    Device/PhysicalDevice.cpp
    Device/Device.cpp
    Descriptor/Descriptor.cpp
    Descriptor/DescriptorFactory.cpp
    Descriptor/DescriptorContext.cpp
    Descriptor/DescriptorPool.cpp
    Fence/Fence.cpp
    Pipeline/PipelineLayout.cpp
    Pipeline/PipelineLayoutDescriptor.cpp
    Pipeline/PipelineLibrary.cpp
    Pipeline/PipelineState.cpp
    Pipeline/ShaderStageFunction.cpp
    Resource/Buffer/BufferMemoryView.cpp
    Resource/Buffer/BufferPool.cpp
    Resource/Buffer/Buffer.cpp
    Resource/Buffer/BufferView.cpp
    Resource/Buffer/IndirectBufferSignature.cpp
    Resource/Constant/ConstantBufferContext.cpp
    Resource/Image/ImagePool.cpp
    Resource/Image/Image.cpp
    Resource/Image/ImageView.cpp
    Resource/Sampler/Sampler.cpp
    Resource/ShaderResource/ShaderResourcePool.cpp
    Resource/ShaderResource/ShaderResource.cpp
    SwapChain/SwapChain.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkRHI
        d3d12
        dxgi
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)