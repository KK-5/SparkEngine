#pragma once

#include <EASTL/unique_ptr.h>

#include <Feature/Window/IWindowSystem.h>
#include <Feature/UI/UIBaseSystem.h>
#include <Engine.h>

#include "Input/EditorInput.h"

namespace Editor
{
    class SparkEditor
    {
    public:
        void Init();
        void Start();
        void Close();

    private:
        eastl::unique_ptr<Spark::SparkEngine>                 m_runtimeEngine;
        eastl::unique_ptr<Spark::Window::IWindowSystem>       m_editorWindow;
        eastl::unique_ptr<EditorInputSystem>                  m_editorInput;
        eastl::unique_ptr<Spark::UI::UIBaseSystem>            m_editorUI;
    };
}