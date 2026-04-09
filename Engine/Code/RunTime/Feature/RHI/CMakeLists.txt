set(TARGET_NAME SparkRHI)

set(SOURCES
    Size.cpp
    Format.cpp
    HardwareQueue.cpp
    MultisampleState.cpp
    ClearValue.cpp
    Factory.cpp
    Attachment/AttachmentLoadStoreAction.cpp
    Attachment/RenderAttachmentLayout.cpp
    Attachment/RenderAttachmentLayoutBuilder.cpp
    Command/CommandList.cpp
    Command/CommandQueue.cpp
    Command/CommandQueueContext.cpp
    Device/Device.cpp
    Device/DeviceObjectPool.cpp
    Device/DeviceObject.cpp
    Fence/Fence.cpp
    Pipeline/PipelineLayoutDescriptor.cpp
    Pipeline/PipelineLibrary.cpp
    Pipeline/PipelineState.cpp
    Pipeline/PipelineStateCache.cpp
    Pipeline/PipelineStateDescriptor.cpp
    Pipeline/RenderStates.cpp
    Pipeline/ShaderStageFunction.cpp
    Resource/Resource.cpp
    Resource/ResourceState.cpp
    Resource/ResourcePool.cpp
    Resource/ResourceView.cpp
    Resource/Buffer/BufferViewDescriptor.cpp
    Resource/Buffer/Buffer.cpp
    Resource/Buffer/BufferPool.cpp
    Resource/Buffer/BufferView.cpp
    Resource/Buffer/IndexBufferView.cpp
    Resource/Buffer/IndirectBufferLayout.cpp
    Resource/Buffer/IndirectBufferSignature.cpp
    Resource/Buffer/IndirectBufferView.cpp
    Resource/Buffer/StreamBufferView.cpp
    Resource/Buffer/VertexBufferView.cpp
    Resource/Image/Image.cpp
    Resource/Image/ImageDescriptor.cpp
    Resource/Image/ImageEnum.cpp
    Resource/Image/ImagePoolBase.cpp
    Resource/Image/ImagePool.cpp
    Resource/Image/ImageSubResource.cpp
    Resource/Image/ImageViewDescriptor.cpp
    Resource/Image/ImageView.cpp
    Resource/Image/StreamingImagePool.cpp
    Resource/Sampler/SamplerState.cpp
    Resource/ShaderResource/ShaderSemantic.cpp
    Resource/ShaderResource/InputStreamLayout.cpp
    Resource/ShaderResource/InputStreamLayoutBuilder.cpp
    Resource/ShaderResource/ConstantsData.cpp
    Resource/ShaderResource/ConstantsLayout.cpp
    Resource/ShaderResource/ShaderResource.cpp
    Resource/ShaderResource/ShaderResourceDescriptor.cpp
    Resource/ShaderResource/ShaderResourceLayout.cpp
    Resource/ShaderResource/ShaderResourceCompiler.cpp
    Scissor/Scissor.cpp
    RenderTargetContext/RenderTargetContext.cpp
    SwapChain/SwapChain.cpp
    Viewport/VIewport.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
)

add_subdirectory(Backend/DX12)

# 指定当前目录的父目录作为包含目录，其他模块包含时必须使用 #include <RHI/...>
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>)