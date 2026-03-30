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
 *  -- SetRenderTargets: AttachmentAccess for depth-stencil (after depthStencil pointer).
 *  -- Clear / DiscardImage: overrides RHI::CommandList; requests are RHI types.
 */

#pragma once

#include <Log/SpdLogSystem.h>

#include <RHI/Command/CommandList.h>
#include <RHI/Command/CommandListStates.h>
#include <RHI/Device/DeviceObjectFactory.h>

#include <Pipeline/PipelineLayout.h>
#include <Pipeline/PipelineState.h>

#include "CommandListBase.h"

namespace Spark::RHI::DX12
{
    class CommandQueue;
    class ShaderResource;
    class SwapChain;

    class CommandList : public RHI::CommandList, 
                        public CommandListBase
    {
    public:
        bool IsInitialized() const;

        void Init(
            Device& device,
            RHI::HardwareQueueClass hardwareQueueClass,
            ID3D12CommandAllocator* commandAllocator);

        void Shutdown() override;

        void Open() override;
        void Close() override;

        //////////////////////////////////////////////////////////////////////////
        // CommandListBase
        void Reset(ID3D12CommandAllocator* commandAllocator) override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // RHI::CommandList
        void SetViewports(const RHI::Viewport* viewports, uint32_t count) override;
        void SetScissors(const RHI::Scissor* scissors, uint32_t count) override;
        void SetShaderResourceForDraw(const RHI::ShaderResource& shaderResource) override;
        void SetShaderResourceForDispatch(const RHI::ShaderResource& shaderResource) override;
        void Submit(const RHI::DrawItem& drawItem, uint32_t submitIndex = 0) override;
        void Submit(const RHI::CopyItem& copyItem, uint32_t submitIndex = 0) override;
        void Submit(const RHI::DispatchItem& dispatchItem, uint32_t submitIndex = 0) override;
        void BeginPredication(const RHI::Buffer& buffer, uint64_t offset, RHI::PredicationOp operation) override;
        void EndPredication() override;
        void QueueBarrier(const RHI::BufferBarrier& barrier) override;
        void QueueBarrier(const RHI::ImageBarrier& barrier) override;
        void FlushBarriers() override;
        void SetFragmentShadingRate(
            RHI::ShadingRate rate,
            const RHI::ShadingRateCombinators& combinators = DefaultShadingRateCombinators) override;

        void SetRenderTargets(
            uint32_t renderTargetCount,
            const RHI::ImageView* const* renderTarget,
            const RHI::ImageView* depthStencil,
            RHI::AttachmentAccess depthStencilAccess,
            const RHI::ImageView* shadingRate) override;

        void ClearRenderTarget(const RHI::ImageClearRequest& request) override;
        void ClearUnorderedAccess(const RHI::ImageClearRequest& request) override;
        void ClearUnorderedAccess(const RHI::BufferClearRequest& request) override;
        void DiscardImage(const RHI::Image& image) override;
        //////////////////////////////////////////////////////////////////////////

    private:
        friend class CommandQueue;

        void SetParentQueue(CommandQueue* commandQueue);

        // NOTE: Uses templates to remove branching. Specialized between draw / dispatch paths.
        // Assigns a shader resource group to a logical slot. Does not bind to the command list
        // (CommitShaderResources does the command list bind).
        template <RHI::PipelineStateType>
        void SetShaderResource(const ShaderResource* shaderResource);

        // NOTE: Uses templates to remove branching. Specialized between draw / dispatch paths.
        // Binds the pipeline state / pipeline layout, then the shader resources associated with a
        // draw / dispatch call. Uses a pull model to bind state to the command list. Returns
        // whether the operation succeeded.
        template <RHI::PipelineStateType, typename Item>
        bool CommitShaderResources(const Item& item);

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
            eastl::array<const ShaderResource*, RHI::Limits::Pipeline::ShaderResourceCountMax> m_srgsByIndex;
            eastl::array<const ShaderResource*, RHI::Limits::Pipeline::ShaderResourceCountMax> m_srgsBySlot;
            bool m_hasRootConstants = false;
            int m_bindlessHeapLastIndex = -1;
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
    };

    template <RHI::PipelineStateType pipelineType>
    void CommandList::SetShaderResource(const ShaderResource* shaderResource)
    {
        if (RHI::Validation::isEnabled)
        {
            if (!shaderResource)
            {
                ASSERT(false, "ShaderResource assigned to draw item is null. This is not allowed.");
                return;
            }
        }

        const uint32_t bindingSlot = shaderResource->GetBindingSlot();

        GetShaderResourceBindingsByPipelineType(pipelineType).m_srgsBySlot[bindingSlot] = shaderResource;
    }
}