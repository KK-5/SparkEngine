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

    //! Create a static-import buffer — uploaded once (or infrequently via streaming)
    //! and used with a single, pre-declared bind usage (e.g. vertex / index /
    //! shader-resource).  The render graph does not require an explicit
    //! ImportBufferAttachment call in every build function; the compiler detects
    //! the resource via StaticImportTag and emits a one-time CopyDst→target-usage
    //! barrier on first access.
    //!
    //! For per-frame CPU-written buffers (e.g. constant / uniform buffers that
    //! are updated every frame) use CreateDynamicBuffer (ImportedTag) instead —
    //! that path participates in the per-pass barrier compile and the per-frame
    //! ResourceStateTracker cursor.
    inline RHIHandle CreateStaticBuffer(
        BasicContext<RHIHandle>& ctx,
        ObjectName name,
        const BufferDescriptor& desc,
        HeapMemoryLevel heapLevel = HeapMemoryLevel::Device,
        HostMemoryAccess hostAccess = HostMemoryAccess::Write)
    {
        RHIHandle entity = ctx.CreateEntity();
        ctx.Add<StaticImportTag>(entity);
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

    //! Create a static-import image — the primary entry point for material
    //! textures, cubemaps, and other images that are uploaded once (or via mip
    //! streaming) and sampled every frame without explicit per-pass registration.
    //! Same StaticImportTag semantics as CreateStaticBuffer above.
    inline RHIHandle CreateStaticImage(
        BasicContext<RHIHandle>& ctx,
        ObjectName name,
        const ImageDescriptor& desc,
        HeapMemoryLevel heapLevel = HeapMemoryLevel::Device,
        HostMemoryAccess hostAccess = HostMemoryAccess::Write)
    {
        RHIHandle entity = ctx.CreateEntity();
        ctx.Add<StaticImportTag>(entity);
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
