#include "ShadowAtlasAllocator.h"

#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

namespace Spark::Render
{
    void ShadowAtlasAllocator::Init(RHI::RHIContext& rhiCtx)
    {
        auto desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead,
            kShadowAtlasResolution, kShadowAtlasResolution, kShadowAtlasFormat);
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        m_image = RHI::CreateImportedImage(rhiCtx, ObjectName("ShadowAtlas"), desc);
        rhiCtx.Add<ShadowAtlasTag>(m_image);
    }

    void ShadowAtlasAllocator::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_image != RHI::NullHandle && rhiCtx.Valid(m_image))
        {
            rhiCtx.DestoryEntity(m_image);
        }
        m_image = RHI::NullHandle;

        // Fresh pools rather than emptied ones: a handle still held somewhere keeps the old
        // pool alive instead of landing in the re-inited id space.
        m_tiles = Ptr<ShadowTilePool>(new ShadowTilePool());
        m_rows  = Ptr<ShadowRowPool>(new ShadowRowPool());
    }
}
