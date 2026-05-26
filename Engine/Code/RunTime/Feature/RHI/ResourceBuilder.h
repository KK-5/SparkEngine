/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <RHI/Component/Component.h>

namespace Spark::RHI
{

    // === Buffer helpers ===

    inline RHIHandle CreateImportedBuffer(
        BasicContext<RHIHandle>& ctx,
        ObjectName name,
        const BufferDescriptor& desc,
        HeapMemoryLevel heapLevel = HeapMemoryLevel::Device,
        HostMemoryAccess hostAccess = HostMemoryAccess::Write)
    {
        RHIHandle entity = ctx.CreateEntity();
        ctx.Add<ImportedTag>(entity);
        ctx.Add<ResourceName>(entity, ResourceName{ name });
        ctx.Add<PendingBufferInit>(entity, PendingBufferInit{ desc, heapLevel, hostAccess });
        return entity;
    }

    inline void RequestBufferUpload(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        const void* data,
        size_t dataSize,
        uint64_t destinationOffset = 0)
    {
        ctx.Add<PendingBufferUpload>(resourceEntity,
            PendingBufferUpload{ data, dataSize, destinationOffset });
        ctx.Add<UploadPendingTag>(resourceEntity);
    }

    inline RHIHandle CreateBufferView(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        ObjectName viewName,
        const BufferViewDescriptor& viewDesc)
    {
        RHIHandle viewEntity = ctx.CreateEntity();
        ctx.Add<ResourceName>(viewEntity, ResourceName{ viewName });
        ctx.Add<BufferViewDescriptor>(viewEntity, viewDesc);
        ctx.Add<ViewHierarchy>(viewEntity,
            ViewHierarchy{ resourceEntity, NullHandle, NullHandle });
        return viewEntity;
    }

    // === Image helpers ===

    inline RHIHandle CreateImportedImage(
        BasicContext<RHIHandle>& ctx,
        ObjectName name,
        const ImageDescriptor& desc,
        HeapMemoryLevel heapLevel = HeapMemoryLevel::Device,
        HostMemoryAccess hostAccess = HostMemoryAccess::Write)
    {
        RHIHandle entity = ctx.CreateEntity();
        ctx.Add<ImportedTag>(entity);
        ctx.Add<ResourceName>(entity, ResourceName{ name });
        ctx.Add<PendingImageInit>(entity, PendingImageInit{ desc, heapLevel, hostAccess });
        return entity;
    }

    inline void RequestImageUpload(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        const void* data,
        size_t dataSize,
        const ImageSubresourceRange& range = {},
        Origin destinationOrigin = {},
        Format sourceFormat = Format::Unknown)
    {
        ctx.Add<PendingImageUpload>(resourceEntity,
            PendingImageUpload{ data, dataSize, range, destinationOrigin, sourceFormat });
        ctx.Add<UploadPendingTag>(resourceEntity);
    }

    inline RHIHandle CreateImageView(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        ObjectName viewName,
        const ImageViewDescriptor& viewDesc)
    {
        RHIHandle viewEntity = ctx.CreateEntity();
        ctx.Add<ResourceName>(viewEntity, ResourceName{ viewName });
        ctx.Add<ImageViewDescriptor>(viewEntity, viewDesc);
        ctx.Add<ViewHierarchy>(viewEntity,
            ViewHierarchy{ resourceEntity, NullHandle, NullHandle });
        return viewEntity;
    }

} // namespace Spark::RHI
