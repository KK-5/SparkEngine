set(TARGET_NAME SparkRuntime)

add_subdirectory(Core)
add_subdirectory(Feature/Input)
add_subdirectory(Feature/Render)
add_subdirectory(Feature/UI)

add_library(${TARGET_NAME} 
    Engine.cpp
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkInput
        SparkRender
        SparkUI
)

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Runtime")

target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)