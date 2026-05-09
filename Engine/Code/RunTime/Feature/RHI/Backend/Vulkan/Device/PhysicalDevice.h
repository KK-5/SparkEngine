/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/bitset.h>

#include <RHI/Device/PhysicalDevice.h>

#include <Vulkan.h>

namespace Spark::RHI::Vulkan
{
    enum class DeviceFeature : uint32_t
    {
        Compatible2dArrayTexture = 0,
        CustomSampleLocation,
        Predication,
        DepthClipEnable,
        ConservativeRaster,
        DrawIndirectCount,
        NullDescriptor,
        SeparateDepthStencil,
        DynamicRendering,
        SubgroupOperation,
        MemoryBudget,
        LoadNoneOp,
        StoreNoneOp,
        Count // Must be last
    };

    // If you change this enum, you also have to update OptionalDeviceExtensionNames in the cpp file!
    enum class OptionalDeviceExtension : uint32_t
    {
        SampleLocation = 0,
        ConditionalRendering,
        MemoryBudget,
        DepthClipEnable,
        ConservativeRasterization,
        Robustness2,
        ShaderImageAtomicInt64,
        AccelerationStructure,
        RayTracingPipeline,
        RayQuery,
        DeferredHostOperations,
        FragmentShadingRate,
        FragmentDensityMap,
        LoadStoreOpNone,
        SubpassMergeFeedback,
        CalibratedTimestamps,
        ExternalMemoryHost,
        Count
    };

    class PhysicalDevice final : RHI::PhysicalDevice
    {
    public:
        void Init(VkPhysicalDevice vkPhysicalDevice);

        VkPhysicalDevice GetNativePhysicalDevice() const;
        const VkPhysicalDeviceMemoryProperties2& GetMemoryProperties() const;
        uint32_t GetVulkanVersion() const;

        bool IsFeatureSupported(DeviceFeature feature) const;
        bool IsOptionalDeviceExtensionSupported(OptionalDeviceExtension extension) const;

        const VkPhysicalDeviceFeatures& GetDeviceFeatures() const;
        const VkPhysicalDeviceProperties& GetDeviceProperties() const;
        const VkPhysicalDeviceConservativeRasterizationPropertiesEXT& GetConservativeRasterProperties() const;
        const VkPhysicalDeviceDepthClipEnableFeaturesEXT& GetDepthClipEnableFeatures() const;
        const VkPhysicalDeviceRobustness2FeaturesEXT& GetRobustness2Features() const;
        const VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT& GetShaderImageAtomicInt64Features() const;
        const VkPhysicalDeviceAccelerationStructurePropertiesKHR& GetAccelerationStructureProperties() const;
        const VkPhysicalDeviceAccelerationStructureFeaturesKHR& GetAccelerationStructureFeatures() const;
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& GetRayTracingPipelineProperties() const;
        const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& GetRayTracingPipelineFeatures() const;
        const VkPhysicalDeviceRayQueryFeaturesKHR& GetRayQueryFeatures() const;
        const VkPhysicalDeviceVulkan12Features& GetVulkan12Features() const;
        const VkPhysicalDeviceVulkan13Features& GetVulkan13Features() const;
        const VkPhysicalDeviceFragmentShadingRateFeaturesKHR& GetFragmentShadingRateFeatures() const;
        const VkPhysicalDeviceFragmentDensityMapFeaturesEXT& GetFragmentDensityMapFeatures() const;
        const VkPhysicalDeviceFragmentDensityMapPropertiesEXT& GetFragmentDensityMapProperties() const;
        const VkPhysicalDeviceFragmentShadingRatePropertiesKHR& GetFragmentShadingRateProperties() const;
        const VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT& GetSubpassMergeFeedbackFeatures() const;
        const VkPhysicalDeviceMemoryBudgetPropertiesEXT& GetMemoryBudgetProperties() const;
        const VkPhysicalDeviceExternalMemoryHostPropertiesEXT& GetExternalMemoryHostProperties() const;

    private:
        ///////////////////////////////////////////////////////////////////
        // RHI::PhysicalDevice
        void Shutdown() override;
        ///////////////////////////////////////////////////////////////////

        void EnableSupportedOptionalExtensions();

        VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties2 m_memoryProperty{};
        VkPhysicalDeviceMemoryBudgetPropertiesEXT m_memoryBudgetProperties{};

        eastl::bitset<static_cast<uint32_t>(OptionalDeviceExtension::Count)> m_optionalExtensions;
        eastl::bitset<static_cast<uint32_t>(DeviceFeature::Count)> m_features;
        VkPhysicalDeviceFeatures m_deviceFeatures{};
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceProperties2 m_deviceProperties2{};
        VkPhysicalDeviceConservativeRasterizationPropertiesEXT m_conservativeRasterProperties{};
        VkPhysicalDeviceDepthClipEnableFeaturesEXT m_depthClipEnableFeatures{};
        VkPhysicalDeviceRobustness2FeaturesEXT m_robustness2Features{};
        VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT m_shaderImageAtomicInt64Features{};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR m_accelerationStructureProperties{};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR m_accelerationStructureFeatures{};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rayTracingPipelineProperties{};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_rayTracingPipelineFeatures{};
        VkPhysicalDeviceRayQueryFeaturesKHR m_rayQueryFeatures{};
        VkPhysicalDeviceVulkan12Features m_vulkan12Features{};
        VkPhysicalDeviceVulkan13Features m_vulkan13Features{};
        VkPhysicalDeviceFragmentShadingRateFeaturesKHR m_fragmentShadingRateFeatures{};
        VkPhysicalDeviceFragmentDensityMapFeaturesEXT m_fragmentDensityMapFeatures{};
        VkPhysicalDeviceFragmentDensityMapPropertiesEXT m_fragmentDensityMapProperties{};
        VkPhysicalDeviceFragmentShadingRatePropertiesKHR m_fragmentShadingRateProperties{};
        VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT m_subpassMergeFeedbackFeatures{};
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT m_externalMemoryHostProperties{};
        uint32_t m_vulkanVersion = 0;
    };
}

