#pragma once

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
        Spark::UniquePtr<Spark::SparkEngine>      m_runtimeEngine;
        Spark::SystemUniquePtr<Spark::Window::IWindowSystem> m_editorWindow;
        Spark::SystemUniquePtr<EditorInputSystem> m_editorInput;
        Spark::SystemUniquePtr<Spark::UI::UIBaseSystem> m_editorUI;
    };
}