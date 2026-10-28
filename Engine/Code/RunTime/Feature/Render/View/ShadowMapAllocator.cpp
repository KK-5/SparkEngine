#include "ShadowMapAllocator.h"

#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Component/Component.h>
#include <RHI/Component/EventComponent.h>
#include <RHI/Context/RHIEvent.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImagePool.h>

namespace Spark::Render
{
    namespace
    {
        constexpr RHI::ImageBindFlags kShadowMapBindFlags =
            RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead;

        Ptr<RHI::ImagePool> InitPool(RHI::Device& device, RHI::ImageBindFlags bindFlags, const char* name)
        {
            RHI::ImagePoolDescriptor descriptor;
            descriptor.m_bindFlags = bindFlags;

            Ptr<RHI::ImagePool> pool = Service<RHI::Factory>::Get()->CreateImagePool();
            pool->SetName(ObjectName(name));
            if (pool->Init(device, descriptor) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[ShadowMapAllocator] {}: image pool init failed.", name);
                return nullptr;
            }
            return pool;
        }

        Ptr<RHI::Image> InitImage(RHI::ImagePool& pool, const RHI::ImageDescriptor& descriptor, const char* name)
        {
            Ptr<RHI::Image> image = Service<RHI::Factory>::Get()->CreateImage();
            image->SetName(ObjectName(name));
            RHI::ImageInitRequest request;
            request.m_image      = image.get();
            request.m_descriptor = descriptor;
            if (pool.InitImage(request) != RHI::ResultCode::Success)
            {
                return nullptr;
            }
            return image;
        }
    }

    bool IsShadowMapReady(RHI::RHIContext& ctx)
    {
        RHI::RHIHandle maps = RHI::NullHandle;
        ctx.GetView<ShadowMapTag>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle e) { maps = e; });
        return RHI::IsResourceReady(ctx, maps);
    }

    RHI::SamplerState ShadowMapSampler()
    {
        RHI::SamplerState sampler = RHI::SamplerState::Create(
            RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp);
        sampler.m_reductionType  = RHI::ReductionType::Comparison;
        // Reversed-Z: lit when the receiver is at or nearer than the stored occluder.
        sampler.m_comparisonFunc = RHI::ComparisonFunc::GreaterEqual;
        return sampler;
    }

    RHI::ImageViewDescriptor ShadowMapView(RHI::Format format)
    {
        RHI::ImageViewDescriptor view = RHI::ImageViewDescriptor::Create(format, 0, 0, 0, kShadowMapCount - 1);
        view.m_isArray = 1;
        return view;
    }

    bool ShadowMapAllocator::Init(RHI::RHIContext& rhiCtx, RHI::Device& device)
    {
        m_pool        = InitPool(device, kShadowMapBindFlags, "ShadowMapPool");
        m_backingPool = InitPool(device, kShadowMapBindFlags | RHI::ImageBindFlags::TileBacking, "ShadowMapBackings");
        if (!m_pool || !m_backingPool)
        {
            Shutdown(rhiCtx);
            return false;
        }

        RHI::ImageDescriptor descriptor = RHI::ImageDescriptor::Create2DArray(
            kShadowMapBindFlags, kShadowMapResolution, kShadowMapResolution, kShadowMapCount, kShadowMapFormat);
        descriptor.m_reserved        = true;
        // Mapped on the graphics queue only.
        descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
        m_image = InitImage(*m_pool, descriptor, "ShadowMaps");
        if (!m_image)
        {
            LOG_ERROR("[ShadowMapAllocator] The reserved shadow map array could not be made.");
            Shutdown(rhiCtx);
            return false;
        }

        const RHI::Size& tileShape = m_image->GetTileShape();
        if (tileShape.m_width == 0 || ShadowResolution(kShadowFinestLevel) % tileShape.m_width != 0
            || ShadowResolution(kShadowFinestLevel) % tileShape.m_height != 0)
        {
            LOG_ERROR("[ShadowMapAllocator] The finest shadow map is not a whole number of {}x{} tiles.",
                      tileShape.m_width, tileShape.m_height);
            Shutdown(rhiCtx);
            return false;
        }

        m_resource = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::Image>(m_resource, RHI::Components::Image{ m_image });
        rhiCtx.Add<RHI::ImportedTag>(m_resource);
        rhiCtx.Add<RHI::ResourceName>(m_resource, RHI::ResourceName{ ObjectName("ShadowMaps") });
        rhiCtx.Add<ShadowMapTag>(m_resource);

        m_indexAllocator = Ptr<Suballocator>(new Suballocator(device, kShadowMapCount));
        return true;
    }

    void ShadowMapAllocator::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_resource != RHI::NullHandle && rhiCtx.Valid(m_resource))
        {
            rhiCtx.DestoryEntity(m_resource);
        }
        m_resource = RHI::NullHandle;
        m_indexAllocator.reset();

        // The array before the backings it maps.
        m_image.reset();
        for (Backing& backing : m_backings)
        {
            backing = Backing{};
        }
        m_backingPool.reset();
        m_pool.reset();
    }

    Suballocation ShadowMapAllocator::AllocateMaps(uint32_t count)
    {
        return m_indexAllocator ? m_indexAllocator->Allocate(count) : Suballocation{};
    }

    bool ShadowMapAllocator::Commit(uint32_t index, uint32_t resolution)
    {
        ASSERT(resolution != 0, "[ShadowMapAllocator] Commit {} with no resolution; Decommit it instead.", index);
        return ChangeResolution(index, resolution);
    }

    void ShadowMapAllocator::Decommit(uint32_t index)
    {
        ChangeResolution(index, 0);
    }

    bool ShadowMapAllocator::ChangeResolution(uint32_t index, uint32_t resolution)
    {
        Backing& current = m_backings[index];
        if (current.m_resolution == resolution)
        {
            return true;
        }

        // Where the new square covers the old, its mapping replaces the old one before any of the
        // frame's work, and the old backing's memory is released frames later: nothing needs to
        // hold it for that part.
        const RHI::Size& tileShape = m_image->GetTileShape();
        const uint32_t   oldTiles  = current.m_resolution / tileShape.m_width;
        Ptr<RHI::Image>  previous  = eastl::move(current.m_image);
        current = Backing{};

        uint32_t newTiles = 0;
        if (resolution != 0)
        {
            Ptr<RHI::Image> backing = InitImage(*m_backingPool,
                RHI::ImageDescriptor::Create2D(kShadowMapBindFlags | RHI::ImageBindFlags::TileBacking,
                    resolution, resolution, kShadowMapFormat),
                "ShadowMapBacking");
            if (backing)
            {
                newTiles = resolution / tileShape.m_width;
                RequestMapping(index, 0, 0, newTiles, newTiles, backing, true);
                current.m_image      = eastl::move(backing);
                current.m_resolution = resolution;
            }
            else
            {
                LOG_ERROR("[ShadowMapAllocator] No memory for a {}² shadow map at {}.", resolution, index);
            }
        }

        // What the old square has beyond the new one: the columns to its right, then the rows
        // below it.
        if (oldTiles > newTiles)
        {
            RequestMapping(index, newTiles, 0, oldTiles - newTiles, oldTiles, previous, false);
            if (newTiles != 0)
            {
                RequestMapping(index, 0, newTiles, newTiles, oldTiles - newTiles, previous, false);
            }
        }
        return current.m_resolution == resolution;
    }

    void ShadowMapAllocator::RequestMapping(uint32_t index, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
        const Ptr<RHI::Image>& backing, bool map)
    {
        RHI::ImageTileMappingRequest request;
        request.m_image                       = m_image;
        request.m_mapping.m_image             = m_image.get();
        // A shadow map is its layer.
        request.m_mapping.m_arraySlice        = static_cast<uint16_t>(index);
        request.m_mapping.m_tileOrigin        = RHI::Origin(x, y, 0);
        request.m_mapping.m_tileExtent        = RHI::Size(width, height, 1);
        request.m_mapping.m_backingImage      = map ? backing.get() : nullptr;
        request.m_mapping.m_backingTileOffset = 0;
        request.m_backing                     = backing;

        auto& events = *RHI::RHIEventExecuteContext::Current();
        events.Add<RHI::ImageTileMappingRequest>(events.CreateEntity(), eastl::move(request));
    }
}
