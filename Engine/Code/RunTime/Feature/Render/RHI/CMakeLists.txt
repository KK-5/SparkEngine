set(TARGET_NAME SparkRHI)

set(SOURCES
    stub.cpp
    Size.cpp
    Format.cpp
    Device/Device.cpp
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
    Resource/Image/ImageSubResource.cpp
    Resource/Image/ImageViewDescriptor.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)