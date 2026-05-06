/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

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
        DescriptorIndexing,
        BufferDeviceAddress,
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
        DrawIndirectCount,
        RelaxedBlockLayout,
        Robustness2,
        ShaderFloat16Int8,
        ShaderAtomicInt64,
        ShaderImageAtomicInt64,
        AccelerationStructure,
        RayTracingPipeline,
        RayQuery,
        ClusterAccelerationStructure,
        BufferDeviceAddress,
        DeferredHostOperations,
        DescriptorIndexing,
        Spirv14,
        ShaderFloatControls,
        FragmentShadingRate,
        FragmentDensityMap,
        Renderpass2,
        TimelineSempahore,
        LoadStoreOpNone,
        SubpassMergeFeedback,
        CalibratedTimestamps,
        ExternalMemoryHost,
        ExternalSemaphore,
        SeparateDepthStencilLayouts,
        Count
    };

    class PhysicalDevice final : RHI::PhysicalDevice
    {
    public:
        

    };
}

