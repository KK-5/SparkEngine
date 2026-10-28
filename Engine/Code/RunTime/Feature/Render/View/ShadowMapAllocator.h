#pragma once

#include <EASTL/array.h>

#include <Base.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Suballocation/Suballocator.h>

#include "ShadowMapLayout.h"

namespace Spark::RHI
{
    class Device;
    class Image;
    class ImagePool;
}

namespace Spark::Render
{
    //! Whether the shadow maps exist. ShadowPass imports them then, so a later pass may read them.
    bool IsShadowMapReady(RHI::RHIContext& ctx);

    //! How a shadow map is read: a receiver's depth compared against the stored occluder's.
    RHI::SamplerState ShadowMapSampler();

    //! Every shadow map, as the array they are layers of: what ShadowPass writes and the
    //! samplers read.
    RHI::ImageViewDescriptor ShadowMapView(RHI::Format format);

    //! The shadow maps: the reserved array they are the layers of, their indices handed out to
    //! lights, and the memory each is backed by. Knows nothing of lights or views: which light
    //! deserves what is ShadowViewSystem's.
    //!
    //! A shadow map's backing is held here, not by whoever holds its index, and goes only with
    //! the request that unmaps it: the whole array is cleared every frame, so a layer still
    //! mapped onto memory let go would have that memory written to after another resource took
    //! it. A shadow map whose holder went without decommitting it keeps its backing until it is
    //! committed again.
    class ShadowMapAllocator
    {
    public:
        bool Init(RHI::RHIContext& rhiCtx, RHI::Device& device);
        //! After the GPU is idle, and once every index's Suballocation is gone.
        void Shutdown(RHI::RHIContext& rhiCtx);

        //! The array, an imported resource.
        RHI::RHIHandle GetResource() const { return m_resource; }

        //! count consecutive shadow maps, the first at the offset, or an empty one.
        Suballocation AllocateMaps(uint32_t count);

        //! Gives the shadow map memory for resolution² from the top left of its layer. False
        //! when none could be had, and then it has none. Commit and Decommit at most once a shadow
        //! map a frame: a frame's mappings are submitted together, and two on one tile are refused.
        bool Commit(uint32_t index, uint32_t resolution);

        //! Takes the shadow map's memory back.
        void Decommit(uint32_t index);

    private:
        //! Commit, or Decommit for 0.
        bool ChangeResolution(uint32_t index, uint32_t resolution);

        struct Backing
        {
            Ptr<RHI::Image> m_image;
            uint32_t        m_resolution = 0;
        };

        //! A box of the layer's tiles, in tiles, onto `backing`, or off it when not `map`. The
        //! request holds `backing` until it is submitted.
        void RequestMapping(uint32_t index, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                            const Ptr<RHI::Image>& backing, bool map);

        Ptr<RHI::ImagePool> m_pool;
        Ptr<RHI::ImagePool> m_backingPool;
        Ptr<RHI::Image>     m_image;
        eastl::array<Backing, kShadowMapCount> m_backings;

        RHI::RHIHandle    m_resource = RHI::NullHandle;
        Ptr<Suballocator> m_indexAllocator;
    };
}
