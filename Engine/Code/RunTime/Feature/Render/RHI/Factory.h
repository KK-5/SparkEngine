#pragma once

#include <Object/Base.h>
#include "Base.h"
#include "Device/PhysicalDevice.h"

namespace Spark::RHI
{
    class Device;
    class DeviceObject;

    class Buffer;
    class BufferPool;
    class BufferView;

    class Image;
    class ImagePool;
    class ImageView;
    class StreamingImagePool;

    class PipelineLibrary;
    class PipelibeState;

    class Commandlist;
    class CommandQueue;

    class Factory
    {
    public:
        Factory() = default;
        virtual ~Factory() = default;

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

        virtual Ptr<PipelineLibrary> CreatePipelineLibrary() = 0;

        virtual Ptr<PipelibeState> CreatePipelineState() = 0;

        virtual Ptr<Commandlist> CreateCommandList() = 0;

        virtual Ptr<CommandQueue> CreateCommandQueue() = 0;

        virtual void DestoryObject(DeviceObject* obj) = 0;
    };
}