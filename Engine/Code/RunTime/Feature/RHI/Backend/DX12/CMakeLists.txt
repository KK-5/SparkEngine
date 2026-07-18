set(TARGET_NAME SparkRHI_DX12)

# --- Minimum Windows SDK (DX12 backend only) ----------------------------------
# The backend uses symbols that only exist in recent Windows SDK headers, e.g.
# D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_DEPTH / _STENCIL (read-only DSV render
# passes) and D3D12_COMPARISON_FUNC_NONE. These are header-only requirements --
# the underlying runtime features are supported by the stock d3d12.dll on current
# Win10/Win11, so no Agility SDK redist is needed. Gate on the current Windows 11
# SDK, which ships all of them and remains back-compatible down to Win10.
set(SPARK_MIN_WINDOWS_SDK "10.0.26100.0")
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND
   CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_LESS SPARK_MIN_WINDOWS_SDK)
    message(FATAL_ERROR
        "Windows SDK ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION} is too old for "
        "SparkEngine's DX12 backend (requires >= ${SPARK_MIN_WINDOWS_SDK}). "
        "Install it via the Visual Studio Installer (Individual components -> "
        "'Windows 11 SDK (10.0.26100.0)').")
endif()

set(SOURCES
    3rdParty/D3D12MA/D3D12MemAlloc.cpp
    DX12.cpp
    MemoryView.cpp
    Conversions.cpp
    ID3D12Factory.cpp
    RHISystem.cpp
    Command/CommandListBase.cpp
    Command/CommandList.cpp
    Command/CommandListPool.cpp
    Command/CommandRecorder.cpp
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
    Resource/ShaderInput/ShaderBindings.cpp
    Resource/ShaderInput/ShaderInputCompiler.cpp
    Resource/Transient/TransientResourcePool.cpp
    SwapChain/SwapChain.cpp
    UI/ImGui.cpp
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