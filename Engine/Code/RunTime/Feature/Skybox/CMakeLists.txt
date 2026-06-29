set(TARGET_NAME SparkSkybox)

set(SOURCES
    SkyboxSystem.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkRHI
        SparkRender
        SparkAssetManager
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}../>)
