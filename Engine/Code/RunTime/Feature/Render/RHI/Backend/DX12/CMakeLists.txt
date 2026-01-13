set(TARGET_NAME SparkRHI_DX12)

set(SOURCES
    3rdParty/D3D12MA/D3D12MemAlloc.cpp
    DX12.cpp
    MemoryView.cpp
    Conversions.cpp
    Device/PhysicalDevice.cpp
    Device/Device.cpp
    Descriptor/Descriptor.cpp
    Descriptor/DescriptorFactory.cpp
    Descriptor/DescriptorContext.cpp
    Descriptor/DescriptorPool.cpp
    Resource/Buffer/BufferMemoryView.cpp
    Resource/Buffer/BufferPool.cpp
    Resource/Buffer/Buffer.cpp
    Resource/Buffer/BufferView.cpp
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