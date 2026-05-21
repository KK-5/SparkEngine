#include "TransientResourcePool.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Image/Image.h>

namespace Spark::RHI
{
    ResultCode TransientResourcePool::Init(Device& device, const TransientResourcePoolDescriptor& descriptor)
    {
        return ResourcePool::Init(
            device, descriptor,
            [this, &device, &descriptor]()
        {
            m_descriptor = descriptor;
            return InitInternal(device, descriptor);
        });
    }

    Image* TransientResourcePool::CreateImage(
        const TransientImageCreateInfo& createInfo,
        const TransientAllocationFence& allocFence)
    {
        if (!ValidateIsInitialized() || !ValidateBatchOpen() || !ValidateFenceWithinPool(allocFence))
        {
            return nullptr;
        }

        Image* image = CreateImageInternal(createInfo, allocFence);

        if (Validation::isEnabled && image && image->GetPool() != this)
        {
            LOG_ERROR("[TransientResourcePool] {} CreateImageInternal returned an image whose pool pointer is not this pool. "
                      "The backend must register new images via InitResource and reuse cached images that are already registered.",
                      GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
        }

        return image;
    }

    Buffer* TransientResourcePool::CreateBuffer(
        const TransientBufferCreateInfo& createInfo,
        const TransientAllocationFence&  allocFence)
    {
        if (!ValidateIsInitialized() || !ValidateBatchOpen() || !ValidateFenceWithinPool(allocFence))
        {
            return nullptr;
        }

        Buffer* buffer = CreateBufferInternal(createInfo, allocFence);

        if (Validation::isEnabled && buffer && buffer->GetPool() != this)
        {
            LOG_ERROR("[TransientResourcePool] {} CreateBufferInternal returned a buffer whose pool pointer is not this pool.",
                      GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
        }

        return buffer;
    }

    void TransientResourcePool::Discard(Image* image, const TransientAllocationFence& discardFence)
    {
        if (!ValidateIsInitialized() || !ValidateBatchOpen())
        {
            return;
        }
        if (!ValidateImageOwnedByThis(image) || !ValidateFenceWithinPool(discardFence))
        {
            return;
        }

        DiscardInternal(image, discardFence);
    }

    void TransientResourcePool::Discard(Buffer* buffer, const TransientAllocationFence& discardFence)
    {
        if (!ValidateIsInitialized() || !ValidateBatchOpen())
        {
            return;
        }
        if (!ValidateBufferOwnedByThis(buffer) || !ValidateFenceWithinPool(discardFence))
        {
            return;
        }

        DiscardInternal(buffer, discardFence);
    }

    void TransientResourcePool::Seal()
    {
        if (!ValidateIsInitialized() || !ValidateBatchOpen())
        {
            return;
        }

        m_batchOpen = false;
    }

    void TransientResourcePool::GetDeviceMemoryBarriers(uint32_t timelinePosition, eastl::vector<DeviceMemoryBarrier>& out) const
    {
        if (!ValidateIsInitialized() || !ValidateBatchSealed())
        {
            return;
        }

        GetDeviceMemoryBarriersInternal(timelinePosition, out);
    }

    TransientResourcePoolStats TransientResourcePool::GetStats() const
    {
        if (!ValidateIsInitialized())
        {
            return {};
        }

        return GetStatsInternal();
    }

    const TransientResourcePoolDescriptor& TransientResourcePool::GetDescriptor() const
    {
        return m_descriptor;
    }

    void TransientResourcePool::OnFrameBegin()
    {
        m_batchOpen = true;
        OnFrameBeginInternal();
        ResourcePool::OnFrameBegin();
    }

    void TransientResourcePool::OnFrameEnd()
    {
        if (Validation::isEnabled && m_batchOpen)
        {
            LOG_ERROR("[TransientResourcePool] {} OnFrameEnd reached with the batch still open. "
                      "Seal() must be called once per frame, after the last Create*/Discard and "
                      "before command recording.",
                      GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
        }

        OnFrameEndInternal();
        ResourcePool::OnFrameEnd();
    }

    void TransientResourcePool::SetImageDescriptor(Image& image, const ImageDescriptor& descriptor)
    {
        image.SetDescriptor(descriptor);
    }

    void TransientResourcePool::SetBufferDescriptor(Buffer& buffer, const BufferDescriptor& descriptor)
    {
        buffer.SetDescriptor(descriptor);
    }

    bool TransientResourcePool::ValidateImageOwnedByThis(const Image* image) const
    {
        if (Validation::isEnabled)
        {
            if (!image || image->GetPool() != this)
            {
                LOG_ERROR("[TransientResourcePool] {} Image is null or was not allocated by this pool.",
                          GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }
        return true;
    }

    bool TransientResourcePool::ValidateBufferOwnedByThis(const Buffer* buffer) const
    {
        if (Validation::isEnabled)
        {
            if (!buffer || buffer->GetPool() != this)
            {
                LOG_ERROR("[TransientResourcePool] {} Buffer is null or was not allocated by this pool.",
                          GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }
        return true;
    }

    bool TransientResourcePool::ValidateBatchOpen() const
    {
        if (Validation::isEnabled)
        {
            if (!m_batchOpen)
            {
                LOG_ERROR("[TransientResourcePool] {} Operation requires an open batch (call must occur between OnFrameBegin and Seal()).",
                          GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }
        return true;
    }

    bool TransientResourcePool::ValidateBatchSealed() const
    {
        if (Validation::isEnabled)
        {
            if (m_batchOpen)
            {
                LOG_ERROR("[TransientResourcePool] {} GetDeviceMemoryBarriers requires the batch to be sealed (call Seal() after the last Create*/Discard).",
                          GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }
        return true;
    }

    bool TransientResourcePool::ValidateFenceWithinPool(const TransientAllocationFence& fence) const
    {
        if (Validation::isEnabled)
        {
            const HardwareQueueClassMask outOfMaskBits = fence.m_pipelines & ~m_descriptor.m_supportedPipelines;
            if (outOfMaskBits != HardwareQueueClassMask::None)
            {
                LOG_ERROR("[TransientResourcePool] {} Fence references hardware queues outside the pool's supported pipeline mask.",
                          GetName().GetCStr() ? GetName().GetCStr() : "[Nameless]");
                return false;
            }
        }
        return true;
    }
}
