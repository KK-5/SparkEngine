
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

option(BUILD_RHI_BAKECUBEMAP "Build RHI BakeCubemap" ON)

if (BUILD_RHI_BAKECUBEMAP)
    set(TARGET_NAME BakeCubemap)

    # Headless compute sample — no window/swapchain, so no glfw.
    add_executable(${TARGET_NAME} RHI/BakeCubemap.cpp)

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
        ENGINE_ASSET_DIR="${ENGINE_ROOT_DIR}/Asset"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
        stb
    )
endif()

option(BUILD_RHI_BRDFLUTGEN "Build RHI BRDFLutGen" ON)

if (BUILD_RHI_BRDFLUTGEN)
    set(TARGET_NAME BRDFLutGen)

    # Headless compute tool — run by hand, writes Engine/Asset/Image/BRDFLut.ktx2.
    add_executable(${TARGET_NAME} RHI/BRDFLutGen.cpp)

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        ENGINE_ASSET_DIR="${ENGINE_ROOT_DIR}/Asset"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
        stb
        ktx
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

option(BUILD_RG_TRIANGLEPASS "Build RenderGraph TrianglePass" ON)

if (BUILD_RG_TRIANGLEPASS)
    set(TARGET_NAME TrianglePass)

    add_executable(${TARGET_NAME}
        Common/SimpleGlfwWindow.cpp
        RenderGraph/TrianglePassFeature.cpp
    )

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
        ENGINE_ASSET_DIR="${ENGINE_ROOT_DIR}/Asset"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        SparkCore
        SparkInput
        SparkRender
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
    )
endif()

option(BUILD_RG_DRAWCUBE "Build RenderGraph DrawCube" ON)

if (BUILD_RG_DRAWCUBE)
    set(TARGET_NAME DrawCube)

    add_executable(${TARGET_NAME}
        Common/SimpleGlfwWindow.cpp
        RenderGraph/DrawCube.cpp
    )

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
        ENGINE_ASSET_DIR="${ENGINE_ROOT_DIR}/Asset"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        SparkCore
        SparkInput
        SparkRender
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
    )
endif()

option(BUILD_RG_MULTIVIEW "Build RenderGraph MultiView" ON)

if (BUILD_RG_MULTIVIEW)
    set(TARGET_NAME MultiView)

    add_executable(${TARGET_NAME}
        Common/SimpleGlfwWindow.cpp
        RenderGraph/MultiView.cpp
    )

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
        ENGINE_ASSET_DIR="${ENGINE_ROOT_DIR}/Asset"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        SparkCore
        SparkInput
        SparkRender
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
    )
endif()

option(BUILD_RG_MSAAPASS "Build RenderGraph MSAAPass" ON)

if (BUILD_RG_MSAAPASS)
    set(TARGET_NAME MSAAPass)

    add_executable(${TARGET_NAME}
        Common/SimpleGlfwWindow.cpp
        RenderGraph/MSAAPassFeature.cpp
    )

    target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SHADER_ASSET_DIR="${SANDBOX_ASSET_DIR}"
        ENGINE_ASSET_DIR="${ENGINE_ROOT_DIR}/Asset"
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        SparkCore
        SparkInput
        SparkRender
        SparkRHI
        SparkRHI_DX12
        SparkAssetManager
    )
endif()