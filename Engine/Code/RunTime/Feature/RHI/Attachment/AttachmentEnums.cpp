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
    AttachmentAccess AdjustAccessBasedOnUsage(AttachmentAccess access, AttachmentUsage usage)
    {
        switch (usage)
        {
        // Remap read/write to write for Color  attachments. This is because from a user standpoint, an attachment
        // might be an input/output to a pass (which maps to read/write) but still be used as a render target (write).
        case AttachmentUsage::RenderTarget:
            if (access == AttachmentAccess::ReadWrite)
            {
                return AttachmentAccess::Write;
            }
            return access;

        // Remap read/write to write for DepthStencil  attachments. This is because from a user standpoint, an attachment
        // might be an input/output to a pass (which maps to read/write) but still be used as a render target (write).
        case AttachmentUsage::DepthStencil:
            if (access == AttachmentAccess::ReadWrite)
            {
                return AttachmentAccess::Write;
            }
            return access;

        // Remap read/write to read for Subpass input  attachments.
        // We disallow write access and throw an error because having a write access on an subpass input attachment is nonsensical.
        case AttachmentUsage::SubpassInput:
            if (access != AttachmentAccess::Read)
            {
                LOG_ERROR("AttachmentAccess cannot be 'Write' when usage is 'SubpassInput'.");
            }
            return AttachmentAccess::Read;

        // Remap write to read/write for Shader  attachments. This is because  
        // a write Shader  is a UAV under the hood, and UAVs are read/write.
        case AttachmentUsage::Shader:
            if (access == AttachmentAccess::Write)
            {
                return AttachmentAccess::ReadWrite;
            }
            return access;

        // Read/write access for Copy  attachments can happen when copying between two devices.
        case AttachmentUsage::Copy:
            return access;

        case AttachmentUsage::InputAssembly:
            if (CheckBitsAll(access, AttachmentAccess::Write))
            {
                LOG_ERROR("AttachmentAccess cannot be 'Write' when usage is 'InputAssembly'.");
            }
            return AttachmentAccess::Read;

        // Remap read/write to read for ShadingRate  attachments.
        // We disallow write access and throw an error because having a write access on an ShadingRate input attachment is not allowed.
        case AttachmentUsage::ShadingRate:
            if (access != AttachmentAccess::Read)
            {
                LOG_ERROR("AttachmentAccess cannot be 'Write' when usage is 'ShadingRate'.");
            }
            return AttachmentAccess::Read;

        // No access adjustment for Resolve or Predication
        case AttachmentUsage::Resolve:
            return access;
        case AttachmentUsage::Predication:
            return access;

        case AttachmentUsage::Indirect:
            if (CheckBitsAll(access, AttachmentAccess::Write))
            {
                LOG_ERROR("AttachmentAccess cannot be 'Write' when usage is 'Indirect'.");
            }
            return AttachmentAccess::Read;

        case AttachmentUsage::Uninitialized:
            return access;
        default:
            ASSERT(false, "Unkown AttachmentUsage: {}", static_cast<uint32_t>(usage));
            return access;
        }
    }
}