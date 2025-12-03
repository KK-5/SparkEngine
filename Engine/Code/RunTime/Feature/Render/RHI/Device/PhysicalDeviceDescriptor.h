/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <cstdint>
#include <EASTL/string.h>
#include <EASTL/array.h>

namespace Spark::Render::RHI
{
    enum class VendorId: uint32_t
    {
        Unknown  =    0,
        Intel    =    0x8086,
        Nvidia   =    0x10de,
        AMD      =    0x1002,
        Qualcomm =    0x5143,
        Samsung  =    0x1099,
        ARM      =    0x13B5,
        Warp     =    0x1414,
        Apple    =    0x106B
    };

    enum class PhysicalDeviceType : uint32_t
    {
        Unknown = 0,
        GpuIntegrated,
        GpuDiscrete,
        GpuVirtual,
        Cpu,
        Count
    };

    class PhysicalDeviceDescriptor
    {
    public:
        virtual ~PhysicalDeviceDescriptor() = default;

        eastl::string m_description;
        PhysicalDeviceType m_type = PhysicalDeviceType::Unknown;
        VendorId m_vendorId = VendorId::Unknown;
        uint32_t m_deviceId = 0;
        uint32_t m_driverVersion = 0;
        //eastl::array<size_t, HeapMemoryLevelCount> m_heapSizePerLevel = {{}};
    };
}