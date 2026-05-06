/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <3rdParty/volk/volk.h>
#include <3rdParty/VMA/include/vk_mem_alloc.h>


namespace Spark::RHI::Vulkan
{
    // using GpuVirtualAddress = VkDeviceAddress;
    using CpuVirtualAddress = uint8_t *;
}