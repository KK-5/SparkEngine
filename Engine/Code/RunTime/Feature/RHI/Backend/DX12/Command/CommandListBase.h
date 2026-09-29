/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove m_aftermathCommandListContext in CommandListBase.
 */

#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>
#include <EASTL/vector.h>

#include <RHI/MultisampleState.h>
#include <RHI/Device/DeviceObject.h>
#include <RHI/HardwareQueue.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    class Device;
    class ImageView;
    class BufferView;

    //! Encapsulates an enhanced barrier with a posible state that is needed for the command list.
    struct BarrierOp
    {
        D3D12_BARRIER_TYPE m_type = D3D12_BARRIER_TYPE_GLOBAL;
        union
        {
            D3D12_GLOBAL_BARRIER  m_global;
            D3D12_TEXTURE_BARRIER m_texture;
            D3D12_BUFFER_BARRIER  m_buffer;
        };
        //! Optional state that the command list needs to be before emitting the barrier.
        eastl::optional<RHI::MultisampleState> m_cmdListState;
    };

    class CommandListBase : public RHI::DeviceObject
    {
     public:
        virtual ~CommandListBase() = default;

        using RHI::DeviceObject::Shutdown;

        CommandListBase(const CommandListBase&) = delete;

        virtual void Reset(ID3D12CommandAllocator* commandAllocator);
        virtual void Close();

        bool IsRecording() const;

        //! Adds a texture barrier that will be emitted when flusing the barriers.
        //! Can specify a state that the command list need to be before emitting the barrier.
        //! A null state means that it doesn't matter in which state the command list is.
        void QueueTextureBarrier(
            const D3D12_TEXTURE_BARRIER& barrier,
            const RHI::MultisampleState* state = nullptr);
        //! Adds a buffer barrier that will be emitted when flusing the barriers.
        void QueueBufferBarrier(const D3D12_BUFFER_BARRIER& barrier);
        //! Adds a global barrier that will be emitted when flusing the barriers.
        void QueueGlobalBarrier(const D3D12_GLOBAL_BARRIER& barrier);

        void FlushBarriers();

        ID3D12GraphicsCommandListX* GetCommandList();

        const ID3D12GraphicsCommandListX* GetCommandList() const;

        RHI::HardwareQueueClass GetHardwareQueueClass() const;
    
    protected:
        void Init(Device& device, RHI::HardwareQueueClass hardwareQueueClass, ID3D12CommandAllocator* commandAllocator);
        //! Sets the state of the command list for emitting a barrier.
        void SetBarrierState(const RHI::MultisampleState& state);
        //! Sets the sample positions of the command list.
        void SetSamplePositions(const RHI::MultisampleState& state);

        CommandListBase() = default;

    private:
        RHI::HardwareQueueClass m_hardwareQueueClass;
        ComPtr<ID3D12GraphicsCommandListX> m_commandList;
        eastl::vector<BarrierOp> m_queuedBarriers;
        bool m_isRecording = false;
        struct State
        {
            RHI::MultisampleState m_customSamplePositions;
        } m_baseState;

        // Nsight Aftermath related command list context
        // void* m_aftermathCommandListContext = nullptr;
    };
}