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
#include <RHI/Component/EventComponent.h>
#include <RHI/Context/RHIEventContext.h>
#include <RHI/Factory.h>

namespace Spark::RHI
{

    //! Is the resource there to be used: its RHI object made (RHIResourceSystem makes it a
    //! frame or two after the entity) and every upload into it over, its copy done. Whoever
    //! reads an uploaded resource asks this first; the Scope that declares the read acquires it.
    inline bool IsResourceReady(BasicContext<RHIHandle>& ctx, RHIHandle handle)
    {
        if (handle == NullHandle || ctx.Has<PendingUploads>(handle))
        {
            return false;
        }
        if (const auto* image = ctx.TryGet<Components::Image>(handle))
        {
            return image->m_image != nullptr;
        }
        if (const auto* buffer = ctx.TryGet<Components::Buffer>(handle))
        {
            return buffer->m_buffer != nullptr;
        }
        // A per-frame resource gets its component once every frame's copy is made; slots past
        // the device's frame count stay empty.
        if (const auto* images = ctx.TryGet<Components::ImagePerFrame>(handle))
        {
            return images->m_images[0] != nullptr;
        }
        if (const auto* buffers = ctx.TryGet<Components::BufferPerFrame>(handle))
        {
            return buffers->m_buffers[0] != nullptr;
        }
        return false;
    }

    // === Buffer helpers ===

    //! A buffer the render graph does not track: filled by an upload (RequestBufferUpload)
    //! and read from then on, by whatever binds or draws it once it is ready. A buffer
    //! passes declare is imported into the graph instead.
    inline RHIHandle CreateBuffer(
        BasicContext<RHIHandle>& ctx,
        ObjectName name,
        const BufferDescriptor& desc,
        HeapMemoryLevel heapLevel = HeapMemoryLevel::Device,
        HostMemoryAccess hostAccess = HostMemoryAccess::Write)
    {
        RHIHandle entity = ctx.CreateEntity();
        ctx.Add<ResourceName>(entity, ResourceName{ name });
        ctx.Add<PendingBufferInit>(entity, PendingBufferInit{ desc, heapLevel, hostAccess });
        return entity;
    }

    namespace Internal
    {
        //! One more of what Counter counts on the resource (PendingUploads, PendingAcquires).
        template<typename Counter>
        void CountUp(BasicContext<RHIHandle>& ctx, RHIHandle resourceEntity)
        {
            if (auto* pending = ctx.TryGet<Counter>(resourceEntity))
            {
                ++pending->m_count;
            }
            else
            {
                ctx.Add<Counter>(resourceEntity, Counter{ 1 });
            }
        }

        //! One less, the component going with the last. The resource may have gone first, and
        //! its count with it.
        template<typename Counter>
        void CountDown(BasicContext<RHIHandle>& ctx, RHIHandle resourceEntity)
        {
            if (!ctx.Valid(resourceEntity))
            {
                return;
            }
            if (auto* pending = ctx.TryGet<Counter>(resourceEntity))
            {
                if (--pending->m_count == 0)
                {
                    ctx.Remove<Counter>(resourceEntity);
                }
            }
        }

        inline RHIHandle GetUploadTarget(RHIEventContext& events, RHIEvent request)
        {
            if (const auto* buffer = events.TryGet<BufferUploadRequest>(request))
            {
                return buffer->m_target;
            }
            if (const auto* image = events.TryGet<ImageUploadRequest>(request))
            {
                return image->m_target;
            }
            return NullHandle;
        }
    }

    //! Asks AsyncUploadSystem to copy `data` into the buffer. The resource carries
    //! PendingUploads until this and every other upload into it is over; whoever reads it
    //! asks that. `data` stays valid until then.
    inline void RequestBufferUpload(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        const void* data,
        size_t dataSize,
        uint64_t destinationOffset = 0)
    {
        auto& events = *RHIEventExecuteContext::Current();
        const RHIEvent request = events.CreateEntity();
        events.Add<BufferUploadRequest>(request,
            BufferUploadRequest{ resourceEntity, data, dataSize, destinationOffset });
        events.Add<UploadRequestedTag>(request);
        Internal::CountUp<PendingUploads>(ctx, resourceEntity);
    }

    //! AsyncUploadSystem's: the upload `request` is over and takes no acquire, whether it
    //! landed or was dropped. Its target has one upload less, and the request goes.
    inline void FinishUploadRequest(BasicContext<RHIHandle>& ctx, RHIEventContext& events, RHIEvent request)
    {
        Internal::CountDown<PendingUploads>(ctx, Internal::GetUploadTarget(events, request));
        events.DestoryEntity(request);
    }

    //! AsyncUploadSystem's: the copy of `request` is over and it awaits its acquire. Its
    //! target has one upload less and one acquire more; the request stays.
    inline void FinishUploadCopy(BasicContext<RHIHandle>& ctx, RHIEventContext& events, RHIEvent request)
    {
        const RHIHandle target = Internal::GetUploadTarget(events, request);
        Internal::CountDown<PendingUploads>(ctx, target);
        if (ctx.Valid(target))
        {
            Internal::CountUp<PendingAcquires>(ctx, target);
        }
    }

    //! The acquire of `request` is recorded, or will never be: its target has one acquire
    //! less, and the request goes. Whatever ends a request awaiting its acquire calls this.
    inline void FinishUploadAcquire(BasicContext<RHIHandle>& ctx, RHIEventContext& events, RHIEvent request)
    {
        Internal::CountDown<PendingAcquires>(ctx, Internal::GetUploadTarget(events, request));
        events.DestoryEntity(request);
    }

    // === Image helpers ===

    //! Image counterpart of CreateBuffer: material textures, cubemaps and the like, uploaded
    //! and then sampled with no pass declaring them. One a pass does declare may start here
    //! too and be imported once its upload is over; from then on it takes no more uploads.
    inline RHIHandle CreateImage(
        BasicContext<RHIHandle>& ctx,
        ObjectName name,
        const ImageDescriptor& desc,
        HeapMemoryLevel heapLevel = HeapMemoryLevel::Device,
        HostMemoryAccess hostAccess = HostMemoryAccess::Write)
    {
        RHIHandle entity = ctx.CreateEntity();
        ctx.Add<ResourceName>(entity, ResourceName{ name });
        ctx.Add<PendingImageInit>(entity, PendingImageInit{ desc, heapLevel, hostAccess });
        return entity;
    }

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

    //! Image counterpart of RequestBufferUpload.
    inline void RequestImageUpload(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        const void* data,
        size_t dataSize,
        const ImageSubresourceRange& range = {},
        Origin destinationOrigin = {},
        Format sourceFormat = Format::Unknown)
    {
        auto& events = *RHIEventExecuteContext::Current();
        const RHIEvent request = events.CreateEntity();
        events.Add<ImageUploadRequest>(request,
            ImageUploadRequest{ resourceEntity, data, dataSize, range, destinationOrigin, sourceFormat });
        events.Add<UploadRequestedTag>(request);
        Internal::CountUp<PendingUploads>(ctx, resourceEntity);
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

    //! Buffer-view counterpart of GetOrCreateImageView (see it for the full rationale).
    //! Resource-owned, descriptor-keyed buffer view cache; on a miss materializes a new
    //! RHI::BufferView from the given buffer and caches it. Returns a pointer stable for
    //! the resource's lifetime (nullptr on init failure). Single-frame only — for
    //! per-frame resources use GetOrCreateBufferViewPerFrame.
    inline RHI::BufferView* GetOrCreateBufferView(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        Buffer& buffer,
        const BufferViewDescriptor& viewDesc)
    {
        Components::BufferViewCache* cache = ctx.TryGet<Components::BufferViewCache>(resourceEntity);
        if (!cache)
        {
            ctx.Add<Components::BufferViewCache>(resourceEntity, Components::BufferViewCache{});
            cache = ctx.TryGet<Components::BufferViewCache>(resourceEntity);
        }

        for (auto& entry : cache->m_entries)
        {
            if (entry.m_descriptor == viewDesc)
            {
                return entry.m_view.get();   // cache hit
            }
        }

        // Miss — materialize a new view and cache it.
        Ptr<RHI::BufferView> view = Service<Factory>::Get()->CreateBufferView();
        ASSERT(view != nullptr, "[GetOrCreateBufferView] Factory::CreateBufferView returned null.");
        if (view->Init(buffer, viewDesc) != ResultCode::Success)
        {
            LOG_ERROR("[GetOrCreateBufferView] BufferView::Init failed.");
            return nullptr;
        }
        RHI::BufferView* raw = view.get();

        cache->m_entries.push_back(
            Components::BufferViewCacheEntry{ viewDesc, eastl::move(view) });
        return raw;
    }

    //! Per-frame counterpart of GetOrCreateBufferView (see GetOrCreateImageViewPerFrame
    //! for the per-frame contract). `buffer` MUST be the frameIndex-th buffer of the
    //! resource. On a cache hit the contract is validated via BufferView::GetBuffer().
    inline RHI::BufferView* GetOrCreateBufferViewPerFrame(
        BasicContext<RHIHandle>& ctx,
        RHIHandle resourceEntity,
        Buffer& buffer,
        const BufferViewDescriptor& viewDesc,
        uint32_t frameIndex)
    {
        ASSERT(frameIndex < RHI::Limits::Device::FrameCountMax,
            "[GetOrCreateBufferViewPerFrame] frameIndex {} out of range.", frameIndex);

        Components::BufferViewCachePerFrame* cache =
            ctx.TryGet<Components::BufferViewCachePerFrame>(resourceEntity);
        if (!cache)
        {
            ctx.Add<Components::BufferViewCachePerFrame>(
                resourceEntity, Components::BufferViewCachePerFrame{});
            cache = ctx.TryGet<Components::BufferViewCachePerFrame>(resourceEntity);
        }

        Components::BufferViewCachePerFrameEntry* entry = nullptr;
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
                Components::BufferViewCachePerFrameEntry{ viewDesc, {} });
            entry = &cache->m_entries.back();
        }

        Ptr<RHI::BufferView>& slot = entry->m_views[frameIndex];
        if (slot)
        {
            ASSERT(&slot->GetBuffer() == &buffer,
                "[GetOrCreateBufferViewPerFrame] cached view for frame {} was built over a "
                "different buffer — (buffer,frameIndex) mismatch or the resource's per-frame "
                "buffer changed without cache invalidation.", frameIndex);
            return slot.get();
        }

        Ptr<RHI::BufferView> view = Service<Factory>::Get()->CreateBufferView();
        ASSERT(view != nullptr, "[GetOrCreateBufferViewPerFrame] Factory::CreateBufferView returned null.");
        if (view->Init(buffer, viewDesc) != ResultCode::Success)
        {
            LOG_ERROR("[GetOrCreateBufferViewPerFrame] BufferView::Init failed.");
            return nullptr;
        }
        slot = eastl::move(view);
        return slot.get();
    }

} // namespace Spark::RHI
