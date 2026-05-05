#pragma once

#include <EASTL/vector.h>

#include <Object/ObjectName.h>

#include <RHI/ClearValue.h>
#include <RHI/Resource/ResourcePool.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

#include "TransientAllocationFence.h"
#include "TransientResourcePoolDescriptor.h"
#include "TransientResourcePoolStats.h"

namespace Spark::RHI
{
    class Resource;
    class Image;
    class Buffer;
    class Device;

    struct TransientImageCreateInfo
    {
        ImageDescriptor   m_descriptor;
        const ClearValue* m_optimizedClearValue = nullptr;
        ObjectName        m_debugName;
    };

    struct TransientBufferCreateInfo
    {
        BufferDescriptor m_descriptor;
        ObjectName       m_debugName;
    };


    //! Pool that allocates short-lived images and buffers from heap memory shared
    //! through aliasing. Resources are valid from their Create*() fence through
    //! their Discard() fence; resources whose fence intervals do not overlap may
    //! share the same heap range.
    //!
    //! Lifecycle is driven by FrameEventBus plus an explicit Seal() call:
    //!   - OnFrameBegin opens a new allocation batch. The previous batch's GPU
    //!     work is guaranteed complete by the engine's frames-in-flight fence,
    //!     so heap memory can be safely recycled.
    //!   - Create*() / Discard() are valid only while the batch is open.
    //!   - Seal() closes the batch. After Seal(), aliasing relationships and
    //!     barriers become queryable through GetAliasingBarriers(). The caller
    //!     must Seal before recording barriers into command lists, so Seal
    //!     belongs in the render-graph compile phase, not at end-of-frame.
    //!   - OnFrameEnd performs end-of-frame bookkeeping; it does not seal
    //!     the batch.
    //!
    //! Returned resources are bare RHI::Image* / RHI::Buffer*. Ownership is
    //! tracked via Resource::m_pool, so destroying the resource routes through
    //! TransientResourcePool::ShutdownResourceInternal in the same way ImagePool
    //! and BufferPool work. Callers above the RHI mark transient resources via
    //! their own ECS tags — the RHI does not introduce a separate transient type.
    //!
    //! Discard() is distinct from ShutdownResource(): Discard only releases the
    //! backing memory range for reuse within the current batch. The Image*/Buffer*
    //! object itself remains valid and is held by the pool's cross-batch cache so
    //! that a later batch can reuse the same placed resource on a descriptor hit.
    class TransientResourcePool : public ResourcePool
    {
    public:
        virtual ~TransientResourcePool() = default;

        ResultCode Init(Device& device, const TransientResourcePoolDescriptor& descriptor);

        //! Allocate a transient image. Returns an Image whose memory is bound and
        //! ready for use from allocFence onwards. The image is owned by this pool;
        //! its destruction routes through ShutdownResourceInternal().
        Image* CreateImage(
            const TransientImageCreateInfo& createInfo,
            const TransientAllocationFence& allocFence);

        //! Allocate a transient buffer. See CreateImage().
        Buffer* CreateBuffer(
            const TransientBufferCreateInfo& createInfo,
            const TransientAllocationFence&  allocFence);

        //! Mark a previously-created image's memory range as no longer in use after
        //! discardFence. The Image* itself remains valid for the rest of the batch;
        //! only the heap range becomes available for re-allocation by subsequent
        //! Create*() calls in the same batch.
        void Discard(Image*  image,  const TransientAllocationFence& discardFence);
        void Discard(Buffer* buffer, const TransientAllocationFence& discardFence);

        //! Close the current allocation batch. After this, Create*() / Discard()
        //! are no longer valid until the next OnFrameBegin, and GetAliasingBarriers()
        //! becomes valid. Call this once per frame, after all transient resources
        //! have been allocated/discarded and before barriers are recorded into
        //! command lists.
        void Seal();

        //! Full aliasing barriers (with src/dst stage populated from Create/Discard
        //! fence data) for the given timeline position. Only valid after Seal()
        //! has closed the batch.
        void GetAliasingBarriers(uint32_t timelinePosition, eastl::vector<AliasingBarrier>& out) const;

        //! Cheap snapshot of pool occupancy and aliasing efficiency.
        TransientResourcePoolStats GetStats() const;

        const TransientResourcePoolDescriptor& GetDescriptor() const override final;

    protected:
        TransientResourcePool() = default;

        void SetImageDescriptor (Image&  image,  const ImageDescriptor&  descriptor);
        void SetBufferDescriptor(Buffer& buffer, const BufferDescriptor& descriptor);

        //////////////////////////////////////////////////////////////////////////
        // FrameEventBus
        void OnFrameBegin() override;
        void OnFrameEnd()   override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // Backend API
        virtual ResultCode InitInternal(
            Device& device,
            const TransientResourcePoolDescriptor& descriptor) = 0;

        virtual Image* CreateImageInternal(
            const TransientImageCreateInfo& createInfo,
            const TransientAllocationFence& allocFence) = 0;

        virtual Buffer* CreateBufferInternal(
            const TransientBufferCreateInfo& createInfo,
            const TransientAllocationFence&  allocFence) = 0;

        virtual void DiscardInternal(
            Image* image,
            const TransientAllocationFence& discardFence) = 0;

        virtual void DiscardInternal(
            Buffer* buffer,
            const TransientAllocationFence& discardFence) = 0;

        virtual void GetAliasingBarriersInternal(
            uint32_t                     timelinePosition,
            eastl::vector<AliasingBarrier>& out) const = 0;

        virtual void OnFrameBeginInternal() {}
        virtual void OnFrameEndInternal()   {}

        virtual TransientResourcePoolStats GetStatsInternal() const { return {}; }
        //////////////////////////////////////////////////////////////////////////

    private:
        using ResourcePool::Init;

        bool ValidateImageOwnedByThis(const Image*  image)  const;
        bool ValidateBufferOwnedByThis(const Buffer* buffer) const;
        bool ValidateBatchOpen() const;
        bool ValidateBatchSealed() const;
        bool ValidateFenceWithinPool(const TransientAllocationFence& fence) const;

        TransientResourcePoolDescriptor m_descriptor;

        //! True between OnFrameBegin and Seal(). Create / Discard are only
        //! valid while open; GetAliasingBarriers requires the batch sealed.
        bool m_batchOpen = false;
    };
}
