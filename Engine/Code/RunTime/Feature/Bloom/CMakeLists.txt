set(TARGET_NAME SparkBloom)

# Components only; the render layer consumes them.
add_library(${TARGET_NAME} INTERFACE)

target_link_libraries(${TARGET_NAME}
    INTERFACE
        SparkCore
)

target_include_directories(
  ${TARGET_NAME}
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)
