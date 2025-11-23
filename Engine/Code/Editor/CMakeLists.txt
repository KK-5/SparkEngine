
set(TARGET_NAME SparkEditor)

add_executable(${TARGET_NAME}
    UI/Private/MenuBar.cpp
    UI/Private/BottomPanel.cpp
    UI/Private/SceneView.cpp
    UI/Private/Inspector.cpp
    UI/Private/ComponentView.cpp
    UI/EditorWindow.cpp
    UI/EditorUI.cpp
    Input/EditorInput.cpp
    Editor.cpp
    main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        glfw
        ImGui
        SparkRuntime
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "SparkEditor")

