
set(TARGET_NAME SparkEditor)

add_executable(${TARGET_NAME}
    UI/Private/MenuBar.cpp
    UI/Private/BottomPanel.cpp
    UI/Private/SceneView.cpp
    UI/Private/Inspector.cpp
    UI/Private/ComponentView.cpp
    Handler/AssetHandler.cpp
    Handler/ComponentAssetResolver.cpp
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

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

set_target_properties(${TARGET_NAME} PROPERTIES
    FOLDER "SparkEditor"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

