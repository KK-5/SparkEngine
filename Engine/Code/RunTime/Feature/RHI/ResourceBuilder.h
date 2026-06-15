/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Component/Component.h>
#include <RHI/Factory.h>

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

    //! Resource-owned, descriptor-keyed image view cache (see Components::ImageViewCache).
    //! Looks up the resource's view cache by descriptor; on a hit returns the cached
    //! view, on a miss materializes a new RHI::ImageView (from the given image) and
    //! caches it. Returns the RHI::ImageView* (nullptr on init failure). The pointer
    //! is stable for the resource's lifetime, so callers may bake it into compile
    //! products.
    //!
    //! The caller supplies the RHI::Image because its owner differs by resource kind
    //! (Components::Image for external/static, the render graph's BackingImage for
    //! transient) and this RHI-layer helper must not depend on the render layer.
    //!
    //! A resource is pure memory; each distinct ImageViewDescriptor on it is one
    //! shared (deduplicated) view. Meant to gradually replace CreateImageView so
    //! callers stop minting a fresh view per use. Append is in-place once the empty
    //! cache is pre-added at resource creation (parallel-friendly per resource).
    //!
    //! Single-frame only — for per-frame (ImagePerFrame) resources use
    //! GetOrCreateImageViewPerFrame.
    inline RHI::ImageView* GetOrCreateImageView(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        Image& image,
        const ImageViewDescriptor& viewDesc)
    {
        Components::ImageViewCache* cache = ctx.TryGet<Components::ImageViewCache>(resourceEntity);
        if (!cache)
        {
            ctx.Add<Components::ImageViewCache>(resourceEntity, Components::ImageViewCache{});
            cache = ctx.TryGet<Components::ImageViewCache>(resourceEntity);
        }

        for (auto& entry : cache->m_entries)
        {
            if (entry.m_descriptor == viewDesc)
            {
                return entry.m_view.get();   // cache hit
            }
        }

        // Miss — materialize a new view and cache it.
        Ptr<RHI::ImageView> view = Service<Factory>::Get()->CreateImageView();
        ASSERT(view != nullptr, "[GetOrCreateImageView] Factory::CreateImageView returned null.");
        if (view->Init(image, viewDesc) != ResultCode::Success)
        {
            LOG_ERROR("[GetOrCreateImageView] ImageView::Init failed.");
            return nullptr;
        }
        RHI::ImageView* raw = view.get();

        cache->m_entries.push_back(
            Components::ImageViewCacheEntry{ viewDesc, eastl::move(view) });
        return raw;
    }

    //! Per-frame counterpart of GetOrCreateImageView, for per-frame resources
    //! (ImagePerFrame / swap chain). The descriptor keys one entry holding N views
    //! (one per frame-in-flight); each slot is built lazily for its frame and reused.
    //! Cache home is Components::ImageViewCachePerFrame.
    //!
    //! Contract: `image` MUST be the frameIndex-th image of the resource — typically
    //! the current BackingImage, which RefreshPerFrameBackings has already rotated to
    //! frameIndex. This call only ever fills the frameIndex slot from `image`, so over
    //! N frames every slot fills and then steady-state reuses. On a cache hit the
    //! contract is validated via ImageView::GetImage() (catches a mismatched
    //! (image,frameIndex) or a per-frame image that changed without cache invalidation).
    //!
    //! Returns the frameIndex view (nullptr on init failure).
    inline RHI::ImageView* GetOrCreateImageViewPerFrame(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        Image& image,
        const ImageViewDescriptor& viewDesc,
        uint32_t frameIndex)
    {
        ASSERT(frameIndex < RHI::Limits::Device::FrameCountMax,
            "[GetOrCreateImageViewPerFrame] frameIndex {} out of range.", frameIndex);

        Components::ImageViewCachePerFrame* cache =
            ctx.TryGet<Components::ImageViewCachePerFrame>(resourceEntity);
        if (!cache)
        {
            ctx.Add<Components::ImageViewCachePerFrame>(
                resourceEntity, Components::ImageViewCachePerFrame{});
            cache = ctx.TryGet<Components::ImageViewCachePerFrame>(resourceEntity);
        }

        Components::ImageViewCachePerFrameEntry* entry = nullptr;
        for (auto& e : cache->m_entries)
        {
            if (e.m_descriptor == viewDesc)
            {
                entry = &e;
                break;
            }
        }
        if (!entry)
        {
            cache->m_entries.push_back(
                Components::ImageViewCachePerFrameEntry{ viewDesc, {} });
            entry = &cache->m_entries.back();
        }

        Ptr<RHI::ImageView>& slot = entry->m_views[frameIndex];
        if (slot)
        {
            ASSERT(&slot->GetImage() == &image,
                "[GetOrCreateImageViewPerFrame] cached view for frame {} was built over a "
                "different image — (image,frameIndex) mismatch or the resource's per-frame "
                "image changed without cache invalidation.", frameIndex);
            return slot.get();
        }

        Ptr<RHI::ImageView> view = Service<Factory>::Get()->CreateImageView();
        ASSERT(view != nullptr, "[GetOrCreateImageViewPerFrame] Factory::CreateImageView returned null.");
        if (view->Init(image, viewDesc) != ResultCode::Success)
        {
            LOG_ERROR("[GetOrCreateImageViewPerFrame] ImageView::Init failed.");
            return nullptr;
        }
        slot = eastl::move(view);
        return slot.get();
    }

} // namespace Spark::RHI
