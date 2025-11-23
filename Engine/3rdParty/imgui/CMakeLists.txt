set(TARGET_NAME ImGui)

set(ALL_SOURCES
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_dx12.cpp
    backends/imgui_impl_opengl3.cpp
    #backends/imgui_impl_vulkan.cpp
)

add_library(${TARGET_NAME} STATIC ${ALL_SOURCES})

target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
)

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})