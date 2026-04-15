#include "Editor.h"

#include <Log/SpdLogSystem.h>
#include <Reflection/TypeRegistry.h>

#include "UI/EditorWindow.h"
#include "UI/EditorUI.h"
#include "Component/Reflect.h"


namespace Editor
{
    void SparkEditor::Init()
    {
        Spark::TypeRegistry::Register(Editor::Reflect);
        Spark::TypeRegistry::RegisterAll();

        m_editorWindow = Spark::CreateSystem<EditorWindow>(1920, 1080, "SparkEditor");
        m_editorWindow->Init();

        m_runtimeEngine = eastl::make_unique<Spark::SparkEngine>();
        m_runtimeEngine->SetUp();

        m_editorUI = Spark::CreateSystem<EditorUI>();
        m_editorUI->Init();

        m_editorInput = Spark::CreateSystem<EditorInputSystem>();
        m_editorInput->Init();
    }

    void SparkEditor::Start()
    {
        m_runtimeEngine->Run([&]()
        { 
            return m_editorWindow->ShouldClose(); 
        });
    }

    void SparkEditor::Close()
    {

    }
}