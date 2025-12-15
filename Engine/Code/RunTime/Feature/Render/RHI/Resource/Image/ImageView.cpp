#include "ImageView.h"

#include <Log/SpdLogSystem.h>

#include "Image.h"

namespace Spark::Render::RHI
{
    ResultCode ImageView::Init(const Image& image, const ImageViewDescriptor& viewDescriptor)
    {
        if (!ValidateForInit(image, viewDescriptor))
        {
            return ResultCode::InvalidOperation;
        }

        m_descriptor = viewDescriptor;
        return ResourceView::Init(image);
    }

    const ImageViewDescriptor& ImageView::GetDescriptor() const
    {
        return m_descriptor;
    }

    const Image& ImageView::GetImage() const
    {
        return static_cast<const Image&>(GetResource());
    }

    bool ImageView::IsFullView() const
    {
        const ImageDescriptor& imageDescriptor = GetImage().GetDescriptor();
        return
            m_descriptor.m_arraySliceMin == 0 &&
            (m_descriptor.m_arraySliceMax + 1) >= imageDescriptor.m_arraySize &&
            m_descriptor.m_mipSliceMin == 0 &&
            (m_descriptor.m_mipSliceMax + 1) >= imageDescriptor.m_mipLevels;
    }

    bool ImageView::ValidateForInit(const Image& image, const ImageViewDescriptor& viewDescriptor) const
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_WARN("[ImageView] Image view already initialized");
                return false;
            }

            if (!image.IsInitialized())
            {
                LOG_WARN("[ImageView] Attempting to create view from uninitialized image '%s'.", image.GetName().GetCStr());
                return false;
            }

            if (!CheckBitsAll(static_cast<uint32_t>(image.GetDescriptor().m_bindFlags), static_cast<uint32_t>(viewDescriptor.m_overrideBindFlags)))
            {
                LOG_WARN("[ImageView] Image view has bind flags that are incompatible with the underlying image.");
                return false;
            }
        }

        return true;
    }
}