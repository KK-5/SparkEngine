/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "AttachmentEnums.h"

#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    AccessFlags ConvertBufferAccess(AttachmentUsage usage, AttachmentAccess access)
    {
        const bool wantsWrite = CheckBitsAny(access, AttachmentAccess::Write);

        switch (usage)
        {
        // A writable Shader attachment is a UAV, which is read/write under the hood.
        case AttachmentUsage::Shader:
            return wantsWrite
                ? (AccessFlags::ShaderStorageRead | AccessFlags::ShaderStorageWrite)
                : (AccessFlags::ConstantBufferRead | AccessFlags::ShaderSampledRead);

        case AttachmentUsage::Copy:
            return wantsWrite ? AccessFlags::TransferWrite : AccessFlags::TransferRead;

        case AttachmentUsage::Indirect:
            return AccessFlags::IndirectRead;

        case AttachmentUsage::InputAssembly:
            return AccessFlags::VertexIndexInput;

        case AttachmentUsage::Predication:
            return AccessFlags::PredicationRead;

        case AttachmentUsage::RayTracingAccelerationStructure:
            return wantsWrite ? AccessFlags::AccelStructWrite : AccessFlags::AccelStructRead;

        default:
            ASSERT(false, "ConvertBufferAccess: usage {} is not valid for a buffer", static_cast<uint32_t>(usage));
            return AccessFlags::None;
        }
    }

    AccessFlags ConvertImageAccess(AttachmentUsage usage, AttachmentAccess access)
    {
        const bool wantsWrite = CheckBitsAny(access, AttachmentAccess::Write);

        switch (usage)
        {
        // DX12 has no color-read-only state; a render target is always a write.
        case AttachmentUsage::RenderTarget:
            return AccessFlags::ColorAttachmentWrite;

        case AttachmentUsage::DepthStencil:
            return wantsWrite ? AccessFlags::DepthStencilWrite : AccessFlags::DepthStencilRead;

        // A writable Shader attachment is a UAV, which is read/write under the hood.
        case AttachmentUsage::Shader:
            return wantsWrite
                ? (AccessFlags::ShaderStorageRead | AccessFlags::ShaderStorageWrite)
                : AccessFlags::ShaderSampledRead;

        case AttachmentUsage::Copy:
            return wantsWrite ? AccessFlags::TransferWrite : AccessFlags::TransferRead;

        case AttachmentUsage::Resolve:
            return wantsWrite ? AccessFlags::ResolveWrite : AccessFlags::ResolveRead;

        case AttachmentUsage::ShadingRate:
            return AccessFlags::ShadingRateRead;

        case AttachmentUsage::Present:
            return AccessFlags::Present;

        default:
            ASSERT(false, "ConvertImageAccess: usage {} is not valid for an image", static_cast<uint32_t>(usage));
            return AccessFlags::None;
        }
    }
}