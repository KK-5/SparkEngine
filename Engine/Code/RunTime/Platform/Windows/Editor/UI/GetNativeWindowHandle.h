#pragma once

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

inline void* GetNativeWindowHandle(GLFWwindow* window) 
{
    return glfwGetWin32Window(window);
}