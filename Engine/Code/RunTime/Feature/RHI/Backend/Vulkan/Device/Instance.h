#pragma once

#include <Vulkan.h>

namespace Spark::RHI::Vulkan
{
    class Instance final
    {
    public:
        Instance() = default;
        ~Instance();


        bool Init();

        void Shutdown();
        
        VkInstance GetNativeInstance()
        {
            return m_instance;
        }
        
    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkInstanceCreateInfo m_instanceCreateInfo = {};
        VkApplicationInfo m_appInfo = {};
        uint32_t m_instanceVersion = 0;
    };
}