#include "Editor.h"

#include <Log/SpdLogSystem.h>
#include <Reflection/TypeRegistry.h>

#include "UI/EditorWindow.h"
#include "UI/EditorUI.h"
#include "Private/Components/Reflect.h"


namespace Editor
{
    void SparkEditor::Init()
    {
        Spark::TypeRegistry::Register(Editor::Reflect);
        Spark::TypeRegistry::RegisterAll();

        m_editorWindow = eastl::make_unique<EditorWindow>(1920, 1080, "SparkEditor");
        m_editorWindow->Initialize();

        m_runtimeEngine = eastl::make_unique<Spark::SparkEngine>();
        m_runtimeEngine->SetUp();

        m_editorUI = eastl::make_unique<EditorUI>();
        m_editorUI->Initialize();

        m_editorInput = eastl::make_unique<EditorInputSystem>();
        m_editorInput->Initialize();
    }

    void SparkEditor::Start()
    {
        m_runtimeEngine->Run([&]() 
            { 
                return m_editorWindow->ShouldClose(); 
            }
        );
    }

    void SparkEditor::Close()
    {
        m_editorInput->ShutDown();
        m_editorUI->ShutDown();
        m_runtimeEngine->ShutDown();
        m_editorWindow->ShutDown();
    }
}