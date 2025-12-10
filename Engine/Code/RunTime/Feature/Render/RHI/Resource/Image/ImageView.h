#pragma once

#include <Resource/ResourceView.h>
#include "ImageViewDescriptor.h"

namespace Spark::Render::RHI
{
    class Image;

    class ImageView : public ResourceView
    {
    public:
        virtual ~ImageView() = default;

        static constexpr uint32_t InvalidBindlessIndex = static_cast<uint32_t>(-1);

        ResultCode Init(const Image& image, const ImageViewDescriptor& viewDescriptor);

        const ImageViewDescriptor& GetDescriptor() const;

        const Image& GetImage() const;

        bool IsFullView() const override final;

        virtual uint32_t GetBindlessReadIndex() const
        {
            return InvalidBindlessIndex;
        }

        virtual uint32_t GetBindlessReadWriteIndex() const
        {
            return InvalidBindlessIndex;
        }
    private:
        bool ValidateForInit(const Image& image, const ImageViewDescriptor& viewDescriptor) const;

        ImageViewDescriptor m_descriptor;
    };
}