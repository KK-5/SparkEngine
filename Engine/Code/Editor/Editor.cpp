#include "Editor.h"

#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>

#include <Window/GLFW/GlfwWindow.h>

#include "UI/EditorWindow.h"
#include "UI/EditorUI.h"
#include "Component/Reflect.h"

#include <Resource/AssetManagerInterface.h>


namespace Editor
{
    void SparkEditor::Init()
    {
        Spark::TypeRegistry::Register(Editor::Reflect);
        Spark::TypeRegistry::RegisterAll();

        m_editorWindow = Spark::CreateSystem<Spark::Window::GlfwWindow>(1920, 1080, "SparkEditor");
        m_editorWindow->Init();

        m_runtimeEngine = eastl::make_unique<Spark::SparkEngine>();
        m_runtimeEngine->SetUp();

        m_editorUI = Spark::CreateSystem<EditorUI>();
        m_editorUI->Init();

        m_editorInput = Spark::CreateSystem<EditorInputSystem>();
        m_editorInput->Init();

        {
            using namespace Spark;
            auto* assetManager = Service<Resource::AssetManager>::Get();
            ASSERT(assetManager, "AssetManager is unregister.");
            assetManager->AddSearchPath("Engine/Code/Test/Resource/Asset");
            assetManager->AddSearchPath("Engine/Code/Editor/Asset");
            assetManager->AssetRegistry();
        }
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