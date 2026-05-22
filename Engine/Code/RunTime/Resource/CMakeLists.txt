set(TARGET_NAME SparkAssetManager)

set(SOURCES
    Asset.cpp
    AssetManager.cpp
    Common/CommonAssetLoader.cpp
    Shader/ShaderAsset.cpp
    Shader/ShaderAssetCompiler.cpp
    Image/ImageAsset.cpp
    Image/ImageAssetLoader.cpp
    Image/ImageAssetCompiler.cpp
    Image/stb_impl.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkRHI
    PRIVATE
        dxc
        stb
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)
