set(TARGET_NAME SparkAssetManager)

set(SOURCES
    Asset.cpp
    AssetTypes.cpp
    AssetBuildContext.cpp
    AssetDataBase.cpp
    AssetManager.cpp
    Common/CommonAssetLoader.cpp
    Shader/ShaderAsset.cpp
    Shader/ShaderAssetCompiler.cpp
    Shader/ShaderAssetBuilder.cpp
    Shader/ShaderBuilder.cpp
    Image/ImageAsset.cpp
    Image/ImageAssetLoader.cpp
    Image/ImageAssetCompiler.cpp
    Image/ImageAssetBuilder.cpp
    Image/EnvironmentBaker.cpp
    Image/stb_impl.cpp
    Model/ModelAsset.cpp
    Model/ModelAssetLoader.cpp
    Model/ModelAssetCompiler.cpp
    Model/ModelAssetBuilder.cpp
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
        nanosvg
        ktx
        fastgltf
        meshoptimizer
        mikktspace
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)
