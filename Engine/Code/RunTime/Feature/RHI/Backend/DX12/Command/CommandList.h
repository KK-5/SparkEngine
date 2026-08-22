/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove DescriptorContext pointer in CommandList, use Factory to get DescriptorContext.
 *  -- Remove CommandList name.
 *  -- Add QueueBarrier/FlushBarriers override from RHI::CommandList.
 *  -- BeginRenderPass / EndRenderPass: dynamic render pass implementation on top of
 *     ID3D12GraphicsCommandList4. Handles MSAA SetSamplePositions, read-only DSV via
 *     AttachmentAccess, optional MSAA resolve (EndingAccess = RESOLVE) and per-region
 *     shading rate image.
 *  -- Clear / DiscardImage: overrides RHI::CommandList; requests are RHI types.
 * Modified by SparkEngine in 2026
 *  -- Simplify ShaderResourceBindings: remove m_srgsBySlot (two-stage assign-then-pull
 *     replaced by direct bind), remove m_bindlessHeapLastIndex, remove m_hasRootConstants.
 *  -- Delete CommitShaderResources template; split into SetPipelineState and
 *     BindShaderInputsForDraw/Dispatch (direct dedup+bind), SetRootConstants removed.
 */

#pragma once

#include <Log/ILogSystem.h>

#include <RHI/Command/CommandList.h>
#include <RHI/Command/CommandListStates.h>
#include <RHI/Device/DeviceObjectFactory.h>

#include <Pipeline/PipelineLayout.h>
#include <Pipeline/PipelineState.h>

#include "CommandListBase.h"

namespace Spark::RHI::DX12
{
    class CommandQueue;
    class ShaderBindings;
    class SwapChain;

    class CommandList : public RHI::CommandList, 
                        public CommandListBase
    {
    public:
        bool IsInitialized() const;

        void Init(
            Device& device,
            RHI::HardwareQueueClass hardwareQueueClass,
            ID3D12CommandAllocatorX* commandAllocator);

        void Shutdown() override;

        void Open() override;
        void Close() override;

        //////////////////////////////////////////////////////////////////////////
        // CommandListBase
        void Reset(ID3D12CommandAllocatorX* commandAllocator) override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // RHI::CommandList
        void SetPipelineState(const RHI::PipelineState& pso) override;
        void SetViewports(const RHI::Viewport* viewports, uint32_t count) override;
        void SetScissors(const RHI::Scissor* scissors, uint32_t count) override;
        void BindShaderInputsForDraw(const RHI::ShaderBindings& bindings) override;
        void BindShaderInputsForDispatch(const RHI::ShaderBindings& bindings) override;
        void Submit(const RHI::DrawItem& drawItem, uint32_t submitIndex = 0) override;
        void Submit(const RHI::CopyItem& copyItem, uint32_t submitIndex = 0) override;
        void Submit(const RHI::DispatchItem& dispatchItem, uint32_t submitIndex = 0) override;
        void BeginPredication(const RHI::Buffer& buffer, uint64_t offset, RHI::PredicationOp operation) override;
        void EndPredication() override;
        void QueueBarrier(const RHI::BufferBarrier& barrier) override;
        void QueueBarrier(const RHI::ImageBarrier& barrier) override;
        void QueueBarrier(const RHI::DeviceMemoryBarrier& barrier) override;
        void FlushBarriers() override;
        void SetFragmentShadingRate(
            RHI::ShadingRate rate,
            const RHI::ShadingRateCombinators& combinators = DefaultShadingRateCombinators) override;

        void BeginRenderPass(const RHI::RenderPassBeginInfo& info) override;
        void EndRenderPass() override;

        void ClearRenderTarget(const RHI::ImageClearRequest& request) override;
        void ClearUnorderedAccess(const RHI::ImageClearRequest& request) override;
        void ClearUnorderedAccess(const RHI::BufferClearRequest& request) override;
        void DiscardImage(const RHI::Image& image) override;
        //////////////////////////////////////////////////////////////////////////

    private:
        friend class CommandQueue;

        void SetParentQueue(CommandQueue* commandQueue);

        // void SetStreamBuffers(const RHI::DeviceGeometryView& geometryView, const RHI::StreamBufferIndices& streamIndices);
        void SetVertexBuffers(const RHI::VertexBufferView& bufferView);
        void SetIndexBuffer(const RHI::IndexBufferView& descriptor);
        void SetStencilRef(uint8_t stencilRef);
        void SetTopology(RHI::PrimitiveTopology topology);
        void CommitViewportState();
        void CommitScissorState();
        void CommitShadingRateState();

        void ExecuteIndirect(const RHI::IndirectArguments& arguments);

        struct ShaderResourceBindings
        {
            const PipelineLayout* m_pipelineLayout = nullptr;
            // Dedup cache: same ShaderBindings re-bound at the same space is a no-op.
            // Indexed by the parallel array index from PipelineLayout::FindSpaceIndexBySpaceId.
            eastl::array<const ShaderBindings*, RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_bindingsBySpace;
        };

        ShaderResourceBindings& GetShaderResourceBindingsByPipelineType(RHI::PipelineStateType pipelineType);

        /**
         * This is kept as a separate struct so that we can robustly reset it. Every property
         * on this struct should be in-class-initialized so that there are no "missed" states.
         * Otherwise, it results in hard-to-track bugs down the road as it's too easy to add something
         * here and then miss adding the initialization elsewhere.
         */
        struct State
        {
            State() = default;

            const RHI::PipelineState* m_pipelineState = nullptr;

            // Graphics-specific state
            eastl::array<uint64_t, RHI::Limits::Pipeline::StreamCountMax> m_streamBufferHashes = {{}};
            uint64_t m_indexBufferHash = 0;
            uint32_t m_stencilRef = static_cast<uint32_t>(-1);
            RHI::PrimitiveTopology m_topology = RHI::PrimitiveTopology::Undefined;
            RHI::CommandListViewportState m_viewportState;
            RHI::CommandListScissorState m_scissorState;
            RHI::CommandListShadingRateState m_shadingRateState;

            // Array of shader resource bindings, indexed by command pipe.
            eastl::array<ShaderResourceBindings, static_cast<size_t>(RHI::PipelineStateType::Count)> m_bindingsByPipe;

            // The command queue assigned to execute the command list.
            CommandQueue* m_parentQueue = nullptr;

            // A queue of tile mappings to execute on the command queue at submission time (prior to executing the command list).
            // TileMapRequestList m_tileMapRequests;

            // The currently bound shading rate image
            const ImageView* m_shadingRateImage = nullptr;

        } m_state;

        // Keep-alive storage for resolve subresource parameters during an active render pass.
        // DX12 requires pSubresourceParameters to stay valid from BeginRenderPass until EndRenderPass.
        // One entry per color attachment; only the ones with an active resolve are populated.
        eastl::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS,
            RHI::Limits::Pipeline::AttachmentColorCountMax> m_resolveSubresourceParams = {};
    };

}