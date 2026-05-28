/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include "Base.h"

namespace Spark::RHI
{
    namespace Limits
    {
        namespace Image
        {
            constexpr uint32_t MipCountMax = 15;
            constexpr uint32_t ArraySizeMax = 2048;
            constexpr uint32_t SizeMax = 16384;
            constexpr uint32_t SizeVolumeMax = 2048;
        }

        namespace Pipeline
        {
            constexpr uint32_t AttachmentColorCountMax = 8;
            constexpr uint32_t ShaderResourceCountMax = 8;
            constexpr uint32_t ShaderInputGroupCountMax = 8;
            constexpr uint32_t StreamCountMax = 12;
            constexpr uint32_t StreamChannelCountMax = 16;
            constexpr uint32_t DrawListTagCountMax = 64;
            constexpr uint32_t DrawFilterTagCountMax = 32;
            constexpr uint32_t MultiSampleCustomLocationsCountMax = 16;
            constexpr uint32_t MultiSampleCustomLocationGridSize = 16;
            constexpr uint32_t SubpassCountMax = 13;
            constexpr uint32_t RenderAttachmentCountMax = 2 * AttachmentColorCountMax + 2; // RenderAttachments + ResolveAttachments + DepthStencilAttachment +  ShadingRateAttachment
            constexpr uint32_t UnboundedArraySize = 100000u;
            constexpr uint32_t RootConstantByteCountMax = 256; // DX12 max 64 DWORDS, Vulkan maxPushConstantsSize >= 128
        }

        namespace Device
        {
            // Maximum number of GPU frames that can be buffered before the CPU will
            // stall. This includes the current frame being built by the CPU. For example,
            // a value of 1 means only single frame is allowed to be build and dispatched at
            // a time. In this example, the CPU timeline would serialize with the GPU timeline
            // because only a single copy of CPU state is available.
            //
            // In a more realistic scenario, a value of 3 would allow the CPU to build the current
            // frame, while the GPU could have up to two frames queued up before the CPU would wait.
            constexpr uint32_t FrameCountMax = 3;

            // Due to the fact that D3D12 only supports the flip model we need to allocate at least
            // a minimum of 2 swapChain images or the drivers will complain.
            constexpr uint32_t MinSwapChainImages = 2;
        }

        namespace APIType
        {
            // RHI::Factory has a virtual method called GetAPIUniqueIndex(), see its documentation
            // for details. GetAPIUniqueIndex() should not return a value greater than this.
            constexpr uint32_t PerPlatformApiUniqueIndexMax = 3;
        }
    } // namespace Limits

    namespace DefaultValues
    {
        namespace Memory
        {
            constexpr uint64_t StagingBufferBudgetInBytes          = 0u;
            constexpr uint64_t AsyncQueueStagingBufferSizeInBytes  = 4ul   * 1024 * 1024;
            constexpr uint64_t MediumStagingBufferPageSizeInBytes  = 2ul   * 1024 * 1024;
            constexpr uint64_t LargestStagingBufferPageSizeInBytes = 128ul * 1024 * 1024;
            constexpr uint64_t ImagePoolPageSizeInBytes            = 2ul   * 1024 * 1024;
            constexpr uint64_t BufferPoolPageSizeInBytes           = 16ul  * 1024 * 1024;
        }
    } // namespace DefaultValues

    // Vulkan binding shifts applied to each HLSL register namespace when compiling
    // HLSL to SPIR-V via DXC. Within a descriptor set (= HLSL space) all binding
    // numbers are flat, so each namespace gets a non-overlapping range.
    //
    // These values must be used in three places and must stay consistent:
    //   1. DXC compilation flags: -fvk-b-shift / -fvk-t-shift / -fvk-u-shift / -fvk-s-shift
    //   2. RHI validation (InsertShaderInput): cross-namespace overlap check
    //   3. Vulkan backend runtime binding: write.dstBinding = Shift_XXX + registerId
    namespace VulkanBindingShift
    {
        constexpr uint32_t CBV     =    0;  // b registers → binding   0 – 999
        constexpr uint32_t SRV     = 1000;  // t registers → binding 1000 – 1999
        constexpr uint32_t UAV     = 2000;  // u registers → binding 2000 – 2999
        constexpr uint32_t Sampler = 3000;  // s registers → binding 3000+
    }

    namespace Alignment
    {
        constexpr uint32_t InputAssembly = 4;
        constexpr uint32_t Constant = 256;
        constexpr uint32_t Buffer = 16;
        
        // for dx12 requirment
        // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT — row pitch alignment for CopyTextureRegion.
        constexpr uint32_t TexturePitch = 256;
        // D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT — source buffer offset alignment for
        // CopyTextureRegion. Source offsets in a staging buffer must be a multiple of this.
        constexpr uint32_t TexturePlacement = 512;
    }
}
