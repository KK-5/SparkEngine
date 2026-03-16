
option(BUILD_RHI_HELLOTRIANGLE "Build RHI HelloTriangle" ON)


if (BUILD_RHI_HELLOTRIANGLE)
    set(TARGET_NAME HelloTriangle)

    add_executable(${TARGET_NAME} RHI/HelloTriangle.cpp)

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkRHI
        SparkRHI_DX12
    )
endif()