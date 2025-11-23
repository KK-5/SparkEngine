#pragma once

#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <HashString/HashString.h>
#include <ECS/ISystem.h>

namespace Spark::Window
{
    enum class WindowBackend
    {
        Invalid,
        GLFW
    };

    class IWindowSystem : public ISystem
    {
    public:
        virtual ~IWindowSystem() = default;

        // ISystem
        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs};
        }

        HashString GetName() const override
        {
            return "WindowSystem"_hs;
        }
        
        //////////////////////////////////////////////////
        virtual void PollEvents()        = 0;
        virtual bool ShouldClose() const = 0;
        virtual void SwapBuffer()        = 0;

        virtual eastl::pair<int, int> GetWindowSize() const   = 0;
        virtual eastl::pair<int, int> GetWindowPos()  const   = 0;
        virtual void*                 GetNativeHandle() const = 0;
        virtual void*                 GetWindowHandle() const = 0;

        virtual WindowBackend GetWindowBackend() const
        {
            return WindowBackend::Invalid;
        }
    };
}