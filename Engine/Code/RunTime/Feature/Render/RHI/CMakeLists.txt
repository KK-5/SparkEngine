set(TARGET_NAME SparkRHI)

set(SOURCES
    stub.cpp
    Size.cpp
    Format.cpp
    Device/Device.cpp
    Device/DeviceObject.cpp
    Resource/Resource.cpp
    Resource/ResourcePool.cpp
    Resource/ResourceView.cpp
    Resource/Buffer/BufferViewDescriptor.cpp
    Resource/Buffer/Buffer.cpp
    Resource/Buffer/BufferPool.cpp
    Resource/Buffer/BufferView.cpp
    Resource/Buffer/BufferViewDescriptor.cpp
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
    Resource/ShaderResource/StreamBufferView.cpp
    Resource/ShaderResource/InputStreamLayout.cpp
    Resource/ShaderResource/InputStreamLayoutBuilder.cpp
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