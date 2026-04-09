
option(BUILD_RHI_HELLOTRIANGLE "Build RHI HelloTriangle" ON)


if (BUILD_RHI_HELLOTRIANGLE)
    set(TARGET_NAME HelloTriangle)

    add_executable(${TARGET_NAME} Common/SimpleGlfwWindow.cpp RHI/HelloTriangle.cpp)

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        SparkCore
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
    )
endif()

option(BUILD_RHI_DRAWSHAPE "Build RHI DrawShape" ON)

if (BUILD_RHI_DRAWSHAPE)
    set(TARGET_NAME DrawShape)

    add_executable(${TARGET_NAME} Common/SimpleGlfwWindow.cpp RHI/DrawShape.cpp)

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        SparkCore
        SparkInput
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
    )
endif()