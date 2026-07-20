set(TARGET_NAME SparkLight)

set(SOURCES
    LightSystem.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkTransform
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)
