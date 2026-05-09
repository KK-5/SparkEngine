#include "Instance.h"

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/allocator.h>
#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>

#include <RHI/Base.h>
#include <RHI/ValidationLayer.h>

namespace Spark::RHI::Vulkan
{
    bool Instance::Init()
    {
        if (volkInitialize() != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to initialize volk.");
            return false;
        }

        eastl::vector<const char*> layers;
        eastl::vector<const char*> extensions;
        if (curValidationMode != RHI::ValidationMode::Disabled)
        {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        }

        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        extensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
        extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

        uint32_t appApiVersion = VK_API_VERSION_1_0;
        m_instanceVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion(&m_instanceVersion) != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Failed to get instance version.");
            return false;
        }
        appApiVersion = VK_API_VERSION_1_3;

        m_appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        m_appInfo.apiVersion = appApiVersion;
        m_appInfo.pEngineName = "SparkEngine";

        m_instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        m_instanceCreateInfo.pApplicationInfo = &m_appInfo;

        // filter layers
        eastl::vector<eastl::string> instanceLayers;
        uint32_t layerPropertyCount = 0;
        VkResult result = vkEnumerateInstanceLayerProperties(&layerPropertyCount, nullptr);
        if (result != VK_SUCCESS || layerPropertyCount == 0)
        {
            LOG_ERROR("[Vulkan] Enumerate instance layer properties failed.");
            return false;
        }
        eastl::vector<VkLayerProperties> layerProperties(layerPropertyCount);
        result = vkEnumerateInstanceLayerProperties(&layerPropertyCount, layerProperties.data());
        if (result != VK_SUCCESS)
        {
            return false;
        }
        instanceLayers.reserve(layerProperties.size());
        for (uint32_t layerPropertyIndex = 0; layerPropertyIndex < layerPropertyCount; ++layerPropertyIndex)
        {
            instanceLayers.emplace_back(layerProperties[layerPropertyIndex].layerName);
        }

        eastl::vector<const char*> finalLayers;
        for (auto& item : layers)
        {
            if (eastl::find(instanceLayers.begin(), instanceLayers.end(), item) != instanceLayers.end())
            {
                finalLayers.push_back(item);
            }
        }

        // Collect available extensions (global + per-layer)
        auto enumerateExtensions = [](eastl::vector<eastl::string>& outExtensions, const char* layerName)
        {
            uint32_t extensionPropertyCount = 0;
            VkResult result = vkEnumerateInstanceExtensionProperties(layerName, &extensionPropertyCount, nullptr);
            if (result != VK_SUCCESS || extensionPropertyCount == 0)
            {
                return;
            }
            eastl::vector<VkExtensionProperties> extensionProperties(extensionPropertyCount);
            result = vkEnumerateInstanceExtensionProperties(layerName, &extensionPropertyCount, extensionProperties.data());
            if (result != VK_SUCCESS)
            {
                return;
            }
            for (uint32_t i = 0; i < extensionPropertyCount; ++i)
            {
                outExtensions.emplace_back(extensionProperties[i].extensionName);
            }
        };

        eastl::vector<eastl::string> availableExtensions;
        enumerateExtensions(availableExtensions, nullptr);
        for (const auto& layer : finalLayers)
        {
            enumerateExtensions(availableExtensions, layer);
        }

        // filter extensions
        eastl::vector<const char*> finalExtensions;
        for (auto& item : extensions)
        {
            if (eastl::find(availableExtensions.begin(), availableExtensions.end(), item) != availableExtensions.end())
            {
                finalExtensions.push_back(item);
            }
            else
            {
                LOG_WARN("[Vulkan] Instance extension '%s' is not available.", item);
            }
        }

        m_instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(finalLayers.size());
        m_instanceCreateInfo.ppEnabledLayerNames = finalLayers.data();
        m_instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(finalExtensions.size());
        m_instanceCreateInfo.ppEnabledExtensionNames = finalExtensions.data();

        result = vkCreateInstance(&m_instanceCreateInfo, nullptr, &m_instance);

        if (result != VK_SUCCESS)
        {
            LOG_ERROR("[Vulkan] Create instance failed.");
            return false;
        }

        volkLoadInstance(m_instance);

        if (curValidationMode != RHI::ValidationMode::Disabled)
        {
            VkDebugUtilsMessengerCreateInfoEXT createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            // createInfo.pfnUserCallback = MessageCallbak;
            createInfo.pUserData = nullptr;
            if (curValidationMode == RHI::ValidationMode::Enabled)
            {
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            }
            else if (curValidationMode == RHI::ValidationMode::Verbose)
            {
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
            }
            else if (curValidationMode == RHI::ValidationMode::GPU)
            {
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
                createInfo.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                createInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
            }
            result = vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger);
            if (result != VK_SUCCESS)
            {
                LOG_ERROR("[Vulkan] Create debug utils messenger failed.");
                return false;
            }
        }

        return true;
    }

    void Instance::Shutdown()
    {
        if (m_debugMessenger != VK_NULL_HANDLE)
        {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }

        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
    }

    Instance::~Instance()
    {
        Shutdown();
    }
}