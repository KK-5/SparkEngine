set(TARGET_NAME SparkRHI_Vulkan)

# Import volk from local 3rdParty folder.
if(NOT TARGET volk::volk)
    set(VOLK_INSTALL OFF CACHE BOOL "Disable volk install target" FORCE)
    set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "Use engine Vulkan target linkage" FORCE)
    add_subdirectory(3rdParty/volk)
    if(TARGET volk)
        set_target_properties(volk PROPERTIES FOLDER "3rdParty/volk")
    endif()
endif()

if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
    set(VMA_ENABLE_INSTALL OFF CACHE BOOL "Disable VMA install target" FORCE)
    set(VMA_BUILD_SAMPLES OFF CACHE BOOL "Disable VMA samples" FORCE)
    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "Disable VMA documentation" FORCE)
    add_subdirectory(3rdParty/VMA)
    if(TARGET VulkanMemoryAllocator)
        set_target_properties(VulkanMemoryAllocator PROPERTIES FOLDER "3rdParty/VMA")
    endif()
endif()

# Collect current backend sources. This allows gradually adding files
# without having to update CMake every time at the bootstrap stage.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "*.cpp"
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkRHI
        VulkanSDK::Vulkan
        volk::volk
        GPUOpen::VulkanMemoryAllocator
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
