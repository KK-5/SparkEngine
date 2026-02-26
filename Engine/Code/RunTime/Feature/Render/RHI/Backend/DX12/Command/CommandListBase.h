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

    //! Encapsulates a resource barrier with a posible state that is needed for the command list.
    struct BarrierOp
    {
        BarrierOp() = default;
        BarrierOp(const D3D12_RESOURCE_TRANSITION_BARRIER& barrier, const RHI::MultisampleState* state)
        {
            m_barrier.Transition = barrier;
            if (state)
            {
                m_cmdListState.emplace(*state);
            }
        }
        BarrierOp(const D3D12_RESOURCE_ALIASING_BARRIER& barrier, const RHI::MultisampleState* state)
        {
            m_barrier.Aliasing = barrier;
            if (state)
            {
                m_cmdListState.emplace(*state);
            }
        }

        //! Resource barrier to be emitted.
        D3D12_RESOURCE_BARRIER m_barrier;
        //! Optional state that the command list needs to be before emitting the barrier.
        eastl::optional<RHI::MultisampleState> m_cmdListState;
    };

    class CommandListBase : public RHI::DeviceObject
    {
     public:
        virtual ~CommandListBase() = 0;

        CommandListBase(const CommandListBase&) = delete;

        virtual void Reset(ID3D12CommandAllocator* commandAllocator);
        virtual void Close();

        bool IsRecording() const;

        //! Adds a transition barrier that will be emitted when flusing the barriers.
        //! Can specify a state that the command list need to be before emitting the barrier.
        //! A null state means that it doesn't matter in which state the command list is.
        void QueueTransitionBarrier(
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES stateBefore,
            D3D12_RESOURCE_STATES stateAfter,
            const RHI::MultisampleState* state = nullptr);
        //! Adds a transition barrier that will be emitted when flusing the barriers.
        //! Can specify a state that the command list need to be before emitting the barrier.
        //! A null state means that it doesn't matter in which state the command list is.
        void QueueTransitionBarrier(
            const D3D12_RESOURCE_TRANSITION_BARRIER& barrier,
            const RHI::MultisampleState* state = nullptr);
        //! Adds a transition barrier operation that will be emitted when flusing the barriers.
        void QueueTransitionBarrier(const BarrierOp& op);
        //! Adds an aliasing barrier that will be emitted when flusing the barriers.
        //! Can specify a state that the command list need to be before emitting the barrier.
        //! A null state means that it doesn't matter in which state the command list is.
        void QueueAliasingBarrier(
            const D3D12_RESOURCE_ALIASING_BARRIER& barrier,
            const RHI::MultisampleState* state = nullptr);
        //! Adds an aliasing barrier operation that will be emitted when flusing the barriers.
        void QueueAliasingBarrier(const BarrierOp& op);

        void FlushBarriers();

        ID3D12GraphicsCommandList* GetCommandList();

        const ID3D12GraphicsCommandList* GetCommandList() const;

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
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandListX> m_commandList;
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