set(TARGET_NAME SparkAssetManager)

set(SOURCES
    Asset.cpp
    AssetTypes.cpp
    AssetBuildContext.cpp
    AssetJsonSerializer.cpp
    AssetDataBase.cpp
    AssetManager.cpp
    Cache/AssetCache.cpp
    Cache/CacheFormat.cpp
    Common/CommonAssetLoader.cpp
    Shader/ShaderAsset.cpp
    Shader/ShaderAssetCompiler.cpp
    Shader/ShaderAssetBuilder.cpp
    Shader/ShaderBuilder.cpp
    Image/ImageAsset.cpp
    Image/ImageAssetLoader.cpp
    Image/ImageAssetCompiler.cpp
    Image/KtxFormatMap.cpp
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

# Folded into every cook-cache key. A Debug and a Release engine can encode the same source
# differently, and neither may read the other's entries; the generator expression keeps them
# apart under the multi-config generators this project builds with.
target_compile_definitions(${TARGET_NAME}
    PRIVATE
        SPARK_CACHE_BUILD_TAG="$<CONFIG>"
)
