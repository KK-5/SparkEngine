/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PhysicalDevice.h"

#include <EASTL/array.h>

#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>

namespace Spark::RHI::Vulkan
{
    // Must match OptionalDeviceExtension enum order
    static const eastl::array<const char*, static_cast<size_t>(OptionalDeviceExtension::Count)> OptionalDeviceExtensionNames =
    {
        VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME,
        VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME,
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
        VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
        VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
        VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
        VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
        VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,
        VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,
        VK_EXT_SUBPASS_MERGE_FEEDBACK_EXTENSION_NAME,
        VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
    };
    static_assert(OptionalDeviceExtensionNames.size() == static_cast<uint32_t>(OptionalDeviceExtension::Count));
    
    void PhysicalDevice::Init(VkPhysicalDevice vkPhysicalDevice)
    {
        m_vkPhysicalDevice = vkPhysicalDevice;
        vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &m_deviceProperties);
        m_vulkanVersion = m_deviceProperties.apiVersion;

        EnableSupportedOptionalExtensions();

        // Memory properties
        m_memoryProperty.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::MemoryBudget))
        {
            m_memoryBudgetProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
            m_memoryProperty.pNext = &m_memoryBudgetProperties;
        }
        vkGetPhysicalDeviceMemoryProperties2(m_vkPhysicalDevice, &m_memoryProperty);

        // Properties structs - only chain those whose extension is supported
        void* propertyChainTail = nullptr;

        auto AppendProperty = [&propertyChainTail](auto& propertyStruct)
        {
            propertyStruct.pNext = propertyChainTail;
            propertyChainTail = &propertyStruct;
        };

        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::ExternalMemoryHost))
        {
            m_externalMemoryHostProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
            AppendProperty(m_externalMemoryHostProperties);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::FragmentDensityMap))
        {
            m_fragmentDensityMapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT;
            AppendProperty(m_fragmentDensityMapProperties);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::FragmentShadingRate))
        {
            m_fragmentShadingRateProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;
            AppendProperty(m_fragmentShadingRateProperties);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::RayTracingPipeline))
        {
            m_rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            AppendProperty(m_rayTracingPipelineProperties);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::AccelerationStructure))
        {
            m_accelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            AppendProperty(m_accelerationStructureProperties);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::ConservativeRasterization))
        {
            m_conservativeRasterProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
            AppendProperty(m_conservativeRasterProperties);
        }

        m_deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        m_deviceProperties2.pNext = propertyChainTail;
        vkGetPhysicalDeviceProperties2(m_vkPhysicalDevice, &m_deviceProperties2);

        // Feature structs - only chain those whose extension is supported
        m_vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        m_vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        void* chainTail = nullptr;

        auto AppendFeature = [&chainTail](auto& featureStruct)
        {
            featureStruct.pNext = chainTail;
            chainTail = &featureStruct;
        };

        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::SubpassMergeFeedback))
        {
            m_subpassMergeFeedbackFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_MERGE_FEEDBACK_FEATURES_EXT;
            AppendFeature(m_subpassMergeFeedbackFeatures);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::FragmentDensityMap))
        {
            m_fragmentDensityMapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
            AppendFeature(m_fragmentDensityMapFeatures);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::FragmentShadingRate))
        {
            m_fragmentShadingRateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
            AppendFeature(m_fragmentShadingRateFeatures);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::RayQuery))
        {
            m_rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            AppendFeature(m_rayQueryFeatures);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::RayTracingPipeline))
        {
            m_rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            AppendFeature(m_rayTracingPipelineFeatures);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::AccelerationStructure))
        {
            m_accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            AppendFeature(m_accelerationStructureFeatures);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::ShaderImageAtomicInt64))
        {
            m_shaderImageAtomicInt64Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
            AppendFeature(m_shaderImageAtomicInt64Features);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::Robustness2))
        {
            m_robustness2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
            AppendFeature(m_robustness2Features);
        }
        if (IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::DepthClipEnable))
        {
            m_depthClipEnableFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
            AppendFeature(m_depthClipEnableFeatures);
        }

        // Core feature structs always chained
        m_vulkan13Features.pNext = chainTail;
        m_vulkan12Features.pNext = &m_vulkan13Features;

        VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.pNext = &m_vulkan12Features;

        vkGetPhysicalDeviceFeatures2(m_vkPhysicalDevice, &deviceFeatures2);
        m_deviceFeatures = deviceFeatures2.features;

        // Evaluate supported features from struct fields
        auto setFeature = [this](DeviceFeature feature, bool supported)
        {
            m_features.set(static_cast<uint32_t>(feature), supported);
        };

        setFeature(DeviceFeature::DrawIndirectCount, m_vulkan12Features.drawIndirectCount);
        setFeature(DeviceFeature::SeparateDepthStencil, m_vulkan12Features.separateDepthStencilLayouts);
        setFeature(DeviceFeature::DynamicRendering, m_vulkan13Features.dynamicRendering);
        setFeature(DeviceFeature::DepthClipEnable, IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::DepthClipEnable) && m_depthClipEnableFeatures.depthClipEnable);
        setFeature(DeviceFeature::NullDescriptor, IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::Robustness2) && m_robustness2Features.nullDescriptor);

        // Features always present in Vulkan 1.3+
        m_features.set(static_cast<uint32_t>(DeviceFeature::Compatible2dArrayTexture), true);
        m_features.set(static_cast<uint32_t>(DeviceFeature::SubgroupOperation), true);

        // Features gated purely by extension support
        m_features.set(static_cast<uint32_t>(DeviceFeature::CustomSampleLocation), IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::SampleLocation));
        m_features.set(static_cast<uint32_t>(DeviceFeature::Predication), IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::ConditionalRendering));
        m_features.set(static_cast<uint32_t>(DeviceFeature::ConservativeRaster), IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::ConservativeRasterization));
        m_features.set(static_cast<uint32_t>(DeviceFeature::MemoryBudget), IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::MemoryBudget));
        m_features.set(static_cast<uint32_t>(DeviceFeature::LoadNoneOp), IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::LoadStoreOpNone));
        m_features.set(static_cast<uint32_t>(DeviceFeature::StoreNoneOp), IsOptionalDeviceExtensionSupported(OptionalDeviceExtension::LoadStoreOpNone));

        m_descriptor.m_description = m_deviceProperties.deviceName;
        switch (m_deviceProperties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            m_descriptor.m_type = RHI::PhysicalDeviceType::GpuDiscrete;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            m_descriptor.m_type = RHI::PhysicalDeviceType::GpuIntegrated;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            m_descriptor.m_type = RHI::PhysicalDeviceType::GpuVirtual;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            m_descriptor.m_type = RHI::PhysicalDeviceType::Cpu;
            break;
        default:
            m_descriptor.m_type = RHI::PhysicalDeviceType::Unknown;
            break;
        }

        m_descriptor.m_vendorId = static_cast<RHI::VendorId>(m_deviceProperties.vendorID);
        m_descriptor.m_deviceId = m_deviceProperties.deviceID;

        for (uint32_t i = 0; i < m_memoryProperty.memoryProperties.memoryTypeCount; ++i)
        {
            const VkMemoryPropertyFlags propertyFlags = m_memoryProperty.memoryProperties.memoryTypes[i].propertyFlags;
            const VkDeviceSize heapSize = m_memoryProperty.memoryProperties.memoryHeaps[m_memoryProperty.memoryProperties.memoryTypes[i].heapIndex].size;

            if (CheckBitsAny(propertyFlags, static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
            {
                size_t index = static_cast<size_t>(RHI::HeapMemoryLevel::Device);
                if (heapSize > m_descriptor.m_heapSizePerLevel[index])
                {
                    m_descriptor.m_heapSizePerLevel[index] = heapSize;
                }
            }

            if (CheckBitsAny(propertyFlags, static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))
            {
                size_t index = static_cast<size_t>(RHI::HeapMemoryLevel::Host);
                if (heapSize > m_descriptor.m_heapSizePerLevel[index])
                {
                    m_descriptor.m_heapSizePerLevel[index] = heapSize;
                }
            }
        }
    }

    void PhysicalDevice::Shutdown()
    {
        m_vkPhysicalDevice = VK_NULL_HANDLE;
    }

    void PhysicalDevice::EnableSupportedOptionalExtensions()
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &extensionCount, nullptr);
        eastl::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &extensionCount, extensions.data());

        uint32_t optionalExtensionCount = static_cast<uint32_t>(OptionalDeviceExtension::Count);
        for (uint32_t i = 0; i < optionalExtensionCount; ++i)
        {
            for (const auto& ext : extensions)
            {
                if (eastl::string_view(ext.extensionName) == OptionalDeviceExtensionNames[i])
                {
                    m_optionalExtensions.set(i);
                    break;
                }
            }
        }
    }

    bool PhysicalDevice::IsOptionalDeviceExtensionSupported(OptionalDeviceExtension extension) const
    {
        uint32_t index = static_cast<uint32_t>(extension);
        ASSERT(index < m_optionalExtensions.size(), "Invalid feature %d", index);
        return m_optionalExtensions.test(index);
    }

    bool PhysicalDevice::IsFeatureSupported(DeviceFeature feature) const
    {
        uint32_t index = static_cast<uint32_t>(feature);
        ASSERT(index < m_features.size(), "Invalid feature %d", index);
        return m_features.test(index);
    }

    VkPhysicalDevice PhysicalDevice::GetNativePhysicalDevice() const
    {
        return m_vkPhysicalDevice;
    }

    const VkPhysicalDeviceMemoryProperties2& PhysicalDevice::GetMemoryProperties() const
    {
        return m_memoryProperty;
    }

    uint32_t PhysicalDevice::GetVulkanVersion() const
    {
        return m_vulkanVersion;
    }

    const VkPhysicalDeviceFeatures& PhysicalDevice::GetDeviceFeatures() const
    {
        return m_deviceFeatures;
    }

    const VkPhysicalDeviceProperties& PhysicalDevice::GetDeviceProperties() const
    {
        return m_deviceProperties;
    }

    const VkPhysicalDeviceConservativeRasterizationPropertiesEXT& PhysicalDevice::GetConservativeRasterProperties() const
    {
        return m_conservativeRasterProperties;
    }

    const VkPhysicalDeviceDepthClipEnableFeaturesEXT& PhysicalDevice::GetDepthClipEnableFeatures() const
    {
        return m_depthClipEnableFeatures;
    }

    const VkPhysicalDeviceRobustness2FeaturesEXT& PhysicalDevice::GetRobustness2Features() const
    {
        return m_robustness2Features;
    }

    const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT& PhysicalDevice::GetShaderImageAtomicInt64Features() const
    {
        return m_shaderImageAtomicInt64Features;
    }

    const VkPhysicalDeviceAccelerationStructurePropertiesKHR& PhysicalDevice::GetAccelerationStructureProperties() const
    {
        return m_accelerationStructureProperties;
    }

    const VkPhysicalDeviceAccelerationStructureFeaturesKHR& PhysicalDevice::GetAccelerationStructureFeatures() const
    {
        return m_accelerationStructureFeatures;
    }

    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& PhysicalDevice::GetRayTracingPipelineProperties() const
    {
        return m_rayTracingPipelineProperties;
    }

    const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& PhysicalDevice::GetRayTracingPipelineFeatures() const
    {
        return m_rayTracingPipelineFeatures;
    }

    const VkPhysicalDeviceRayQueryFeaturesKHR& PhysicalDevice::GetRayQueryFeatures() const
    {
        return m_rayQueryFeatures;
    }

    const VkPhysicalDeviceVulkan12Features& PhysicalDevice::GetVulkan12Features() const
    {
        return m_vulkan12Features;
    }

    const VkPhysicalDeviceVulkan13Features& PhysicalDevice::GetVulkan13Features() const
    {
        return m_vulkan13Features;
    }

    const VkPhysicalDeviceFragmentShadingRateFeaturesKHR& PhysicalDevice::GetFragmentShadingRateFeatures() const
    {
        return m_fragmentShadingRateFeatures;
    }

    const VkPhysicalDeviceFragmentDensityMapFeaturesEXT& PhysicalDevice::GetFragmentDensityMapFeatures() const
    {
        return m_fragmentDensityMapFeatures;
    }

    const VkPhysicalDeviceFragmentDensityMapPropertiesEXT& PhysicalDevice::GetFragmentDensityMapProperties() const
    {
        return m_fragmentDensityMapProperties;
    }

    const VkPhysicalDeviceFragmentShadingRatePropertiesKHR& PhysicalDevice::GetFragmentShadingRateProperties() const
    {
        return m_fragmentShadingRateProperties;
    }

    const VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT& PhysicalDevice::GetSubpassMergeFeedbackFeatures() const
    {
        return m_subpassMergeFeedbackFeatures;
    }

    const VkPhysicalDeviceMemoryBudgetPropertiesEXT& PhysicalDevice::GetMemoryBudgetProperties() const
    {
        return m_memoryBudgetProperties;
    }

    const VkPhysicalDeviceExternalMemoryHostPropertiesEXT& PhysicalDevice::GetExternalMemoryHostProperties() const
    {
        return m_externalMemoryHostProperties;
    }
}