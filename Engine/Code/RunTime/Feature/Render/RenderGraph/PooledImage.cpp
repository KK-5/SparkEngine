#include "PooledImage.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImagePool.h>

namespace Spark::Render
{
    bool IsSameImageStorage(const RHI::ImageDescriptor& a, const RHI::ImageDescriptor& b)
    {
        return a.m_size      == b.m_size
            && a.m_format    == b.m_format
            && a.m_bindFlags == b.m_bindFlags
            && a.m_dimension == b.m_dimension
            && a.m_arraySize == b.m_arraySize
            && a.m_mipLevels == b.m_mipLevels
            && a.m_isCubemap == b.m_isCubemap;
    }

    RHIHandle AcquirePooledImage(
        RHIContext&                 ctx,
        RHI::ImagePool&             pool,
        const RHI::ImageDescriptor& desc,
        const RHI::ClearValue*      optimizedClearValue,
        const ObjectName&           debugName)
    {
        RHIHandle pooled = NullHandle;
        for (auto [entity, pooledDesc] : ctx.GetView<PooledImageTag, RHI::ImageDescriptor>(
                 Exclude<PreviousFrameOf, PooledImageActiveTag>).each())
        {
            if (IsSameImageStorage(pooledDesc, desc))
            {
                pooled = entity;
                break;
            }
        }

        if (pooled == NullHandle)
        {
            auto* factory = Service<RHI::Factory>::Get();
            ASSERT(factory != nullptr, "RHI::Factory service is not registered.");

            Ptr<RHI::Image> image = factory->CreateImage();
            image->SetName(debugName);

            RHI::ImageInitRequest request;
            request.m_image               = image.get();
            request.m_descriptor          = desc;
            request.m_optimizedClearValue = optimizedClearValue;
            const RHI::ResultCode result = pool.InitImage(request);
            ASSERT(result == RHI::ResultCode::Success,
                "[PooledImage] Failed to allocate pooled image '{}'.", debugName.GetCStr());

            pooled = ctx.CreateEntity();
            ctx.Add<PooledImageTag>(pooled);
            ctx.Add<RHI::ImageDescriptor>(pooled, desc);
            ctx.Add<BackingImage>(pooled, BackingImage{ image.get() });
            ctx.Add<Image>(pooled, Image{ eastl::move(image) });
        }

        ctx.Add<PooledImageActiveTag>(pooled);
        return pooled;
    }
}
