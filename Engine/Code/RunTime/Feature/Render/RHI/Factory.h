#pragma once

#include <Object/Base.h>
#include "Base.h"
#include "Device/PhysicalDevice.h"

namespace Spark::Render::RHI
{
    class Device;

    class Buffer;
    class BufferPool;
    class BufferView;

    class Image;
    class ImagePool;
    class ImageView;
    class StreamingImagePool;

    class Factory
    {
        Factory() = default;
        virtual ~Factory() = default;

        Factory(const Factory&) = delete;
        Factory& operator=(const Factory&) = delete;
        Factory(Factory&&) = delete;
        Factory& operator=(Factory&&) = delete;

        //virtual APIIndex GetType() = 0;
        virtual PhysicalDeviceList EnumeratePhysicalDevices() = 0;

        virtual Ptr<Device> CreateDevice() = 0;

        virtual Ptr<Buffer> CreateBuffer() = 0;

        virtual Ptr<BufferPool> CreateBufferPool() = 0;

        virtual Ptr<BufferView> CreateBufferView() = 0;

        virtual Ptr<Image> CreateImage() = 0;

        virtual Ptr<ImagePool> CreateImagePool() = 0;

        virtual Ptr<ImageView> CreateImageView() = 0;

        virtual Ptr<StreamingImagePool> CreateStreamingImagePool() = 0;
    };
}