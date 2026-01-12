set(TARGET_NAME SparkRHI_DX12)

set(SOURCES

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