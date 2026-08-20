/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Add QueueBarrier/FlushBarriers implementation, delegates to CommandListBase.
 * Modified by SparkEngine in 2026
 *  -- CommitShaderResources removed; Submit(DrawItem/DispatchItem) rewritten to call
 *     SetPipelineState / SetShaderResourceForDraw / SetShaderResourceForDispatch directly.
 *  -- SetShaderResourceForDraw/Dispatch: direct binding via srg->GetBindingSlot() →
 *     pipelineLayout->GetIndexBySlot() → m_srgsByIndex dedup → DX12 bind.
 *  -- SetPipelineState: unconditional PSO bind (no dedup), root signature update
 *     on layout change, SRG cache invalidation.
 */

#include "CommandList.h"

#include <Log/ILogSystem.h>

#include <ID3D12Factory.h>
#include <Conversions.h>
#include <Device/Device.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/ShaderInput/ShaderBindings.h>
#include <Resource/Buffer/Buffer.h>
#include <Resource/Buffer/BufferView.h>
#include <Resource/Buffer/IndirectBufferSignature.h>
#include <Resource/Image/Image.h>
#include <Resource/Image/ImageView.h>
#include <SwapChain/SwapChain.h>

#include "CommandQueue.h"

namespace Spark::RHI::DX12
{
    void CommandList::Init(Device& device, RHI::HardwareQueueClass hardwareQueueClass, ID3D12CommandAllocatorX* commandAllocator)
    {
        CommandListBase::Init(device, hardwareQueueClass, commandAllocator);

        if (GetHardwareQueueClass() != RHI::HardwareQueueClass::Copy)
        {
            auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
            descriptorContext.SetDescriptorHeaps(GetCommandList());
        }
    }

    bool CommandList::IsInitialized() const
    {
        return CommandListBase::IsInitialized();
    }

    void CommandList::Shutdown()
    {
        ASSERT(IsInitialized(), "[CommandList] Shutdown an uninitialized CommandList");
    }

    void CommandList::Reset(ID3D12CommandAllocatorX* commandAllocator)
    {
        CommandListBase::Reset(commandAllocator);

        if (GetHardwareQueueClass() != RHI::HardwareQueueClass::Copy)
        {
            Device& device = static_cast<Device&>(GetDevice());
            auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
            descriptorContext.SetDescriptorHeaps(GetCommandList());
        }

        // Clear state back to empty.
        m_state = State();
    }

    void CommandList::Open()
    {
        // Use PIXBeginEvent mark the command, now it is undefine.
    }

    void CommandList::Close()
    {
        FlushBarriers();
        CommandListBase::Close();
    }

    void CommandList::SetParentQueue(CommandQueue* parentQueue)
    {
        m_state.m_parentQueue = parentQueue;
    }

    CommandList::ShaderResourceBindings& CommandList::GetShaderResourceBindingsByPipelineType(RHI::PipelineStateType pipelineType)
    {
        return m_state.m_bindingsByPipe[static_cast<size_t>(pipelineType)];
    }

    void CommandList::SetViewports(
        const RHI::Viewport* viewports,
        uint32_t count)
    {
        m_state.m_viewportState.Set(eastl::span<const RHI::Viewport>(viewports, count));
    }

    void CommandList::SetScissors(
        const RHI::Scissor* scissors,
        uint32_t count)
    {
        m_state.m_scissorState.Set(eastl::span<const RHI::Scissor>(scissors, count));
    }

    void CommandList::SetPipelineState(const RHI::PipelineState& pipelineState)
    {
        const PipelineState& pso = static_cast<const PipelineState&>(pipelineState);

        if (!pso.IsInitialized())
        {
            LOG_WARN("[CommandList] Pipeline State is not initialized.");
            return;
        }

        const PipelineLayout* pipelineLayout = pso.GetPipelineLayout();
        if (!pipelineLayout)
        {
            ASSERT(false, "Pipeline layout is null.");
            return;
        }

        m_state.m_pipelineState = &pipelineState;

        const RHI::PipelineStateType pipelineType = pso.GetType();
        ShaderResourceBindings& bindings = GetShaderResourceBindingsByPipelineType(pipelineType);

        GetCommandList()->SetPipelineState(pso.Get());

        if (pipelineType == RHI::PipelineStateType::Draw)
        {
            const auto& pipelineData = pso.GetPipelineStateData();
            auto& multisampleState = pipelineData.m_drawData.m_multisampleState;
            SetSamplePositions(multisampleState);
            SetTopology(pipelineData.m_drawData.m_primitiveTopology);
        }

        if (bindings.m_pipelineLayout != pipelineLayout)
        {
            switch (pipelineType)
            {
            case RHI::PipelineStateType::Draw:
                GetCommandList()->SetGraphicsRootSignature(pipelineLayout->Get());
                break;

            case RHI::PipelineStateType::Dispatch:
                GetCommandList()->SetComputeRootSignature(pipelineLayout->Get());
                break;

            default:
                ASSERT(false, "Invalid PipelineType");
                return;
            }

            bindings.m_pipelineLayout = pipelineLayout;

            for (size_t i = 0; i < bindings.m_bindingsBySpace.size(); ++i)
            {
                bindings.m_bindingsBySpace[i] = nullptr;
            }
        }
    }

    void CommandList::BindShaderInputsForDraw(const RHI::ShaderBindings& base)
    {
        const ShaderBindings* b = static_cast<const ShaderBindings*>(&base);

        ShaderResourceBindings& bindings = GetShaderResourceBindingsByPipelineType(RHI::PipelineStateType::Draw);
        const PipelineLayout* pipelineLayout = bindings.m_pipelineLayout;
        if (!pipelineLayout)
        {
            ASSERT(false, "Pipeline layout is null. SetPipelineState must be called before binding shader inputs.");
            return;
        }

        const int32_t spaceIdx = pipelineLayout->FindSpaceIndexBySpaceId(b->GetSpaceId());
        if (spaceIdx < 0)
        {
            ASSERT(false, "[CommandList] ShaderBindings spaceId %u not in current PipelineLayout.", b->GetSpaceId());
            return;
        }

        if (bindings.m_bindingsBySpace[spaceIdx] == b)
        {
            return;
        }
        bindings.m_bindingsBySpace[spaceIdx] = b;

        const SpaceCBVBinding&   cbv = pipelineLayout->GetSpaceCBVBinding(static_cast<uint32_t>(spaceIdx));
        const SpaceTableBinding& tbl = pipelineLayout->GetSpaceTableBinding(static_cast<uint32_t>(spaceIdx));
        const ShaderBindingsCompiledData& cd = b->GetCompiledData();

        if (tbl.m_resourceTable != InvalidRootParameterIndex && cd.m_gpuViewsDescriptorHandle.ptr)
        {
            GetCommandList()->SetGraphicsRootDescriptorTable(tbl.m_resourceTable, cd.m_gpuViewsDescriptorHandle);
        }
        if (tbl.m_samplerTable != InvalidRootParameterIndex && cd.m_gpuSamplersDescriptorHandle.ptr)
        {
            GetCommandList()->SetGraphicsRootDescriptorTable(tbl.m_samplerTable, cd.m_gpuSamplersDescriptorHandle);
        }

        ASSERT(cbv.m_rootIndices.size() == cd.m_gpuConstantAddresses.size(),
            "[CommandList] CBV root indices count (%u) != compiled GPU addresses count (%u).",
            static_cast<uint32_t>(cbv.m_rootIndices.size()),
            static_cast<uint32_t>(cd.m_gpuConstantAddresses.size()));
        for (size_t k = 0; k < cbv.m_rootIndices.size(); ++k)
        {
            GetCommandList()->SetGraphicsRootConstantBufferView(
                cbv.m_rootIndices[k], cd.m_gpuConstantAddresses[k]);
        }
    }

    void CommandList::BindShaderInputsForDispatch(const RHI::ShaderBindings& base)
    {
        const ShaderBindings* b = static_cast<const ShaderBindings*>(&base);

        ShaderResourceBindings& bindings = GetShaderResourceBindingsByPipelineType(RHI::PipelineStateType::Dispatch);
        const PipelineLayout* pipelineLayout = bindings.m_pipelineLayout;
        if (!pipelineLayout)
        {
            ASSERT(false, "Pipeline layout is null. SetPipelineState must be called before binding shader inputs.");
            return;
        }

        const int32_t spaceIdx = pipelineLayout->FindSpaceIndexBySpaceId(b->GetSpaceId());
        if (spaceIdx < 0)
        {
            ASSERT(false, "[CommandList] ShaderBindings spaceId %u not in current PipelineLayout.", b->GetSpaceId());
            return;
        }

        if (bindings.m_bindingsBySpace[spaceIdx] == b)
        {
            return;
        }
        bindings.m_bindingsBySpace[spaceIdx] = b;

        const SpaceCBVBinding&   cbv = pipelineLayout->GetSpaceCBVBinding(static_cast<uint32_t>(spaceIdx));
        const SpaceTableBinding& tbl = pipelineLayout->GetSpaceTableBinding(static_cast<uint32_t>(spaceIdx));
        const ShaderBindingsCompiledData& cd = b->GetCompiledData();

        if (tbl.m_resourceTable != InvalidRootParameterIndex && cd.m_gpuViewsDescriptorHandle.ptr)
        {
            GetCommandList()->SetComputeRootDescriptorTable(tbl.m_resourceTable, cd.m_gpuViewsDescriptorHandle);
        }
        if (tbl.m_samplerTable != InvalidRootParameterIndex && cd.m_gpuSamplersDescriptorHandle.ptr)
        {
            GetCommandList()->SetComputeRootDescriptorTable(tbl.m_samplerTable, cd.m_gpuSamplersDescriptorHandle);
        }

        ASSERT(cbv.m_rootIndices.size() == cd.m_gpuConstantAddresses.size(),
            "[CommandList] CBV root indices count (%u) != compiled GPU addresses count (%u).",
            static_cast<uint32_t>(cbv.m_rootIndices.size()),
            static_cast<uint32_t>(cd.m_gpuConstantAddresses.size()));
        for (size_t k = 0; k < cbv.m_rootIndices.size(); ++k)
        {
            GetCommandList()->SetComputeRootConstantBufferView(
                cbv.m_rootIndices[k], cd.m_gpuConstantAddresses[k]);
        }
    }

    void CommandList::Submit(const RHI::CopyItem& copyItem, uint32_t submitIndex)
    {
        ValidateSubmitIndex(submitIndex);

        switch (copyItem.m_type)
        {
            case RHI::CopyItemType::Buffer:
            {
                const RHI::CopyBufferDescriptor& descriptor = copyItem.m_buffer;
                const Buffer* sourceBuffer = static_cast<const Buffer*>(descriptor.m_sourceBuffer);
                const Buffer* destinationBuffer = static_cast<const Buffer*>(descriptor.m_destinationBuffer);

                GetCommandList()->CopyBufferRegion(
                    destinationBuffer->GetMemoryView().GetMemory(),
                    destinationBuffer->GetMemoryView().GetOffset() + descriptor.m_destinationOffset,
                    sourceBuffer->GetMemoryView().GetMemory(),
                    sourceBuffer->GetMemoryView().GetOffset() + descriptor.m_sourceOffset,
                    descriptor.m_size);
                break;
            }
            case RHI::CopyItemType::Image:
            {
                const RHI::CopyImageDescriptor& descriptor = copyItem.m_image;
                const Image* sourceImage = static_cast<const Image*>(descriptor.m_sourceImage);
                const Image* destinationImage = static_cast<const Image*>(descriptor.m_destinationImage);

                const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(
                    sourceImage->GetMemoryView().GetMemory(),
                    D3D12CalcSubresource(
                        descriptor.m_sourceSubresource.m_mipSlice,
                        descriptor.m_sourceSubresource.m_arraySlice,
                        ConvertImageAspectToPlaneSlice(descriptor.m_sourceSubresource.m_aspect),
                        sourceImage->GetDescriptor().m_mipLevels,
                        sourceImage->GetDescriptor().m_arraySize));

                const CD3DX12_TEXTURE_COPY_LOCATION destinationLocation(
                    destinationImage->GetMemoryView().GetMemory(),
                    D3D12CalcSubresource(
                        descriptor.m_destinationSubresource.m_mipSlice,
                        descriptor.m_destinationSubresource.m_arraySlice,
                        ConvertImageAspectToPlaneSlice(descriptor.m_destinationSubresource.m_aspect),
                        destinationImage->GetDescriptor().m_mipLevels,
                        destinationImage->GetDescriptor().m_arraySize));

                const CD3DX12_BOX sourceBox(
                    descriptor.m_sourceOrigin.m_left,
                    descriptor.m_sourceOrigin.m_top,
                    descriptor.m_sourceOrigin.m_front,
                    descriptor.m_sourceOrigin.m_left + descriptor.m_sourceSize.m_width,
                    descriptor.m_sourceOrigin.m_top + descriptor.m_sourceSize.m_height,
                    descriptor.m_sourceOrigin.m_front + descriptor.m_sourceSize.m_depth);

                GetCommandList()->CopyTextureRegion(
                    &destinationLocation,
                    descriptor.m_destinationOrigin.m_left,
                    descriptor.m_destinationOrigin.m_top,
                    descriptor.m_destinationOrigin.m_front,
                    &sourceLocation,
                    &sourceBox);

                break;
            }
            case RHI::CopyItemType::BufferToImage:
            {
                const RHI::CopyBufferToImageDescriptor& descriptor = copyItem.m_bufferToImage;
                const Buffer* sourceBuffer = static_cast<const Buffer*>(descriptor.m_sourceBuffer);
                const Image* destinationImage = static_cast<const Image*>(descriptor.m_destinationImage);

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
                footprint.Offset = sourceBuffer->GetMemoryView().GetOffset() + descriptor.m_sourceOffset;
                footprint.Footprint.Width = descriptor.m_sourceSize.m_width;
                footprint.Footprint.Height = descriptor.m_sourceSize.m_height;
                footprint.Footprint.Depth = descriptor.m_sourceSize.m_depth;
                footprint.Footprint.Format = ConvertFormat(descriptor.m_sourceFormat);
                footprint.Footprint.RowPitch = descriptor.m_sourceBytesPerRow;

                const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(sourceBuffer->GetMemoryView().GetMemory(), footprint);

                const CD3DX12_TEXTURE_COPY_LOCATION destinationLocation(
                    destinationImage->GetMemoryView().GetMemory(),
                    D3D12CalcSubresource(
                        descriptor.m_destinationSubresource.m_mipSlice,
                        descriptor.m_destinationSubresource.m_arraySlice,
                        ConvertImageAspectToPlaneSlice(descriptor.m_destinationSubresource.m_aspect),
                        destinationImage->GetDescriptor().m_mipLevels,
                        destinationImage->GetDescriptor().m_arraySize));

                GetCommandList()->CopyTextureRegion(
                    &destinationLocation,
                    descriptor.m_destinationOrigin.m_left,
                    descriptor.m_destinationOrigin.m_top,
                    descriptor.m_destinationOrigin.m_front,
                    &sourceLocation,
                    nullptr);

                break;
            }
            case RHI::CopyItemType::ImageToBuffer:
            {
                const RHI::CopyImageToBufferDescriptor& descriptor = copyItem.m_imageToBuffer;
                const Image* sourceImage = static_cast<const Image*>(descriptor.m_sourceImage);
                const Buffer* destinationBuffer = static_cast<const Buffer*>(descriptor.m_destinationBuffer);

                const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(
                    sourceImage->GetMemoryView().GetMemory(),
                    D3D12CalcSubresource(
                        descriptor.m_sourceSubresource.m_mipSlice,
                        descriptor.m_sourceSubresource.m_arraySlice,
                        ConvertImageAspectToPlaneSlice(descriptor.m_sourceSubresource.m_aspect),
                        sourceImage->GetDescriptor().m_mipLevels,
                        sourceImage->GetDescriptor().m_arraySize));

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
                footprint.Offset = destinationBuffer->GetMemoryView().GetOffset() + descriptor.m_destinationOffset;
                footprint.Footprint.Width = descriptor.m_sourceSize.m_width;
                footprint.Footprint.Height = descriptor.m_sourceSize.m_height;
                footprint.Footprint.Depth = descriptor.m_sourceSize.m_depth;
                footprint.Footprint.Format = ConvertFormat(descriptor.m_destinationFormat);
                footprint.Footprint.RowPitch = descriptor.m_destinationBytesPerRow;

                const CD3DX12_TEXTURE_COPY_LOCATION destinationLocation(destinationBuffer->GetMemoryView().GetMemory(), footprint);
                GetCommandList()->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
                break;
            }
            case RHI::CopyItemType::QueryToBuffer:
            {
                /*
                const RHI::CopyQueryToBufferDescriptor& descriptor = copyItem.m_queryToBuffer;

                GetCommandList()->ResolveQueryData(
                    static_cast<const QueryPool*>(descriptor.m_sourceQueryPool)->GetHeap(),
                    ConvertQueryType(descriptor.m_sourceQueryPool->GetDescriptor().m_type, RHI::QueryControlFlags::None),
                    descriptor.m_firstQuery.GetIndex(),
                    descriptor.m_queryCount,
                    static_cast<const Buffer*>(descriptor.m_destinationBuffer)->GetMemoryView().GetMemory(),
                    descriptor.m_destinationOffset);
                */
                break;
            }
            default:
            {
                ASSERT(false, "Invalid CopyItem type");
                return;
            }

        }
    }

    void CommandList::Submit(const RHI::DispatchItem& dispatchItem, uint32_t submitIndex)
    {
        ValidateSubmitIndex(submitIndex);

        if (dispatchItem.m_pipelineState)
        {
            SetPipelineState(*dispatchItem.m_pipelineState);
        }

        switch (dispatchItem.m_arguments.m_type)
        {
        case RHI::DispatchType::Direct:
        {
            const auto& directArguments = dispatchItem.m_arguments.m_direct;
            GetCommandList()->Dispatch(directArguments.GetNumberOfGroupsX(), directArguments.GetNumberOfGroupsY(), directArguments.GetNumberOfGroupsZ());
            break;
        }
        case RHI::DispatchType::Indirect:
            ExecuteIndirect(dispatchItem.m_arguments.m_indirect);
            break;
        default:
            ASSERT(false, "Invalid dispatch type");
            break;
        }
    }

    void CommandList::Submit(const RHI::DrawItem& drawItem, uint32_t submitIndex)
    {
        ValidateSubmitIndex(submitIndex);

        SetVertexBuffers(drawItem.m_vertexBufferView);
        SetStencilRef(drawItem.m_stencilRef);

        // Viewport / scissor come from the DrawList the executer already applied.
        CommitScissorState();
        CommitViewportState();
        CommitShadingRateState();

        switch (drawItem.m_drawArguments.m_type)
        {
            case RHI::DrawType::Indexed:
            {
                ASSERT(drawItem.m_indexBufferView.GetBuffer(), "Index buffer view is null!");

                const RHI::DrawIndexed& indexed = drawItem.m_drawArguments.m_indexed;
                SetIndexBuffer(drawItem.m_indexBufferView);

                GetCommandList()->DrawIndexedInstanced(
                    indexed.m_indexCount,
                    drawItem.m_drawInstanceArgs.m_instanceCount,
                    indexed.m_indexOffset,
                    indexed.m_vertexOffset,
                    drawItem.m_drawInstanceArgs.m_instanceOffset);
                break;
            }
            case RHI::DrawType::Linear:
            {
                const RHI::DrawLinear& linear = drawItem.m_drawArguments.m_linear;
                GetCommandList()->DrawInstanced(
                    linear.m_vertexCount,
                    drawItem.m_drawInstanceArgs.m_instanceCount,
                    linear.m_vertexOffset,
                    drawItem.m_drawInstanceArgs.m_instanceOffset);
                break;
            }
            case RHI::DrawType::Indirect:
            {
                const auto& indirect = drawItem.m_drawArguments.m_indirect;
                const RHI::IndirectBufferLayout& layout = indirect.m_indirectBufferView->GetSignature()->GetDescriptor().m_layout;
                if (layout.GetType() == RHI::IndirectBufferLayoutType::IndexedDraw)
                {
                    ASSERT(drawItem.m_indexBufferView.GetBuffer(), "Index buffer view is null!");
                    SetIndexBuffer(drawItem.m_indexBufferView);
                }
                ExecuteIndirect(indirect);
                break;
            }
            default:
            {
                ASSERT(false, "Invalid draw type {}", static_cast<uint32_t>(drawItem.m_drawArguments.m_type));
                break;
            }
        }
    }

    void CommandList::BeginPredication(const RHI::Buffer& buffer, uint64_t offset, RHI::PredicationOp operation)
    {
        GetCommandList()->SetPredication(
            static_cast<const Buffer&>(buffer).GetMemoryView().GetMemory(), 
            offset, 
            ConvertPredicationOp(operation));

    }

    void CommandList::EndPredication()
    {
        GetCommandList()->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    }

    void CommandList::QueueBarrier(const RHI::BufferBarrier& barrier)
    {
        // Cross-queue ownership transfer (Vulkan QFOT) maps to a Common-state
        // bridge in DX12: release side transitions srcAccess -> COMMON, acquire
        // side transitions COMMON -> dstAccess. COMMON is the only state both
        // queues are guaranteed to support transitioning into / out of, so the
        // bridge is universally safe regardless of dstAccess's queue affinity.
        // Cross-queue happens-before is provided by the timeline-semaphore
        // wait that the render layer inserts between the two queues.
        const auto myQueue       = GetHardwareQueueClass();
        const bool isCrossQueue  = (barrier.m_srcQueue != barrier.m_dstQueue);

        if (isCrossQueue)
        {
            ASSERT(myQueue == barrier.m_srcQueue || myQueue == barrier.m_dstQueue,
                "Cross-queue buffer barrier emitted on queue {} that is neither srcQueue {} nor dstQueue {}.",
                static_cast<uint32_t>(myQueue),
                static_cast<uint32_t>(barrier.m_srcQueue),
                static_cast<uint32_t>(barrier.m_dstQueue));
        }

        RHI::BufferPool& bufferPool = static_cast<RHI::BufferPool&>(*barrier.m_buffer->GetPool());
        if (bufferPool.GetDescriptor().m_heapMemoryLevel == RHI::HeapMemoryLevel::Host)
        {
            // Upload / readback heaps live in GENERIC_READ permanently; no transition needed.
            return;
        }

        Buffer& buffer = static_cast<Buffer&>(*barrier.m_buffer);
        ID3D12Resource* resource = buffer.GetMemoryView().GetMemory();

        if (!isCrossQueue)
        {
            CommandListBase::QueueTransitionBarrier(
                resource,
                ConvertBufferState(barrier.m_srcAccess, myQueue, barrier.m_srcStage),
                ConvertBufferState(barrier.m_dstAccess, myQueue, barrier.m_dstStage));
            RHI::CommandList::SetResourceState(*barrier.m_buffer,
                RHI::ResourceState{ barrier.m_dstAccess, myQueue, barrier.m_dstStage });
        }
        else if (myQueue == barrier.m_srcQueue)
        {
            // Release half: srcAccess -> COMMON on the releasing queue. Deliberately
            // does NOT touch the tracked ResourceState — see the ImageBarrier
            // overload for the full rationale (the release races its cross-queue
            // acquire on another queue/thread and, landing last, would park the
            // resource at a transit state the render graph then re-acquires).
            CommandListBase::QueueTransitionBarrier(
                resource,
                ConvertBufferState(barrier.m_srcAccess, myQueue, barrier.m_srcStage),
                D3D12_RESOURCE_STATE_COMMON);
        }
        else
        {
            // Acquire: COMMON -> dstAccess on the destination queue.
            CommandListBase::QueueTransitionBarrier(
                resource,
                D3D12_RESOURCE_STATE_COMMON,
                ConvertBufferState(barrier.m_dstAccess, myQueue, barrier.m_dstStage));
            RHI::CommandList::SetResourceState(*barrier.m_buffer,
                RHI::ResourceState{ barrier.m_dstAccess, myQueue, barrier.m_dstStage });
        }
    }

    void CommandList::QueueBarrier(const RHI::ImageBarrier& barrier)
    {
        // Cross-queue handling: see BufferBarrier overload above.
        const auto myQueue       = GetHardwareQueueClass();
        const bool isCrossQueue  = (barrier.m_srcQueue != barrier.m_dstQueue);

        if (isCrossQueue)
        {
            ASSERT(myQueue == barrier.m_srcQueue || myQueue == barrier.m_dstQueue,
                "Cross-queue image barrier emitted on queue {} that is neither srcQueue {} nor dstQueue {}.",
                static_cast<uint32_t>(myQueue),
                static_cast<uint32_t>(barrier.m_srcQueue),
                static_cast<uint32_t>(barrier.m_dstQueue));
        }

        Image& image = static_cast<Image&>(*barrier.m_image);
        ID3D12Resource* resource = image.GetMemoryView().GetMemory();

        if (!isCrossQueue)
        {
            CommandListBase::QueueTransitionBarrier(
                resource,
                ConvertImageState(barrier.m_srcAccess, myQueue, barrier.m_srcStage),
                ConvertImageState(barrier.m_dstAccess, myQueue, barrier.m_dstStage));
            RHI::CommandList::SetResourceState(*barrier.m_image,
                RHI::ResourceState{ barrier.m_dstAccess, myQueue, barrier.m_dstStage });
        }
        else if (myQueue == barrier.m_srcQueue)
        {
            // Release half: srcAccess -> COMMON on the releasing queue. Deliberately
            // does NOT touch the tracked ResourceState. The release runs on a
            // different queue/thread than the matching acquire (e.g. async upload's
            // Copy queue vs the graphics acquire); writing the transit COMMON here
            // races the acquire and, landing last, would leave the resource parked
            // at {Uninitialized, srcQueue}. The render-graph compile then re-reads
            // that as "still needs a cross-queue acquire" and re-emits a COMMON→target
            // barrier onto an already-transitioned resource (D3D12 error #527). The
            // destination queue's acquire is the sole authority for the post-handoff
            // state; the source queue the acquire needs for its cross-queue detection
            // is already carried by the pre-release tracked state.
            CommandListBase::QueueTransitionBarrier(
                resource,
                ConvertImageState(barrier.m_srcAccess, myQueue, barrier.m_srcStage),
                D3D12_RESOURCE_STATE_COMMON);
        }
        else
        {
            // Acquire: COMMON -> dstAccess on the destination queue.
            CommandListBase::QueueTransitionBarrier(
                resource,
                D3D12_RESOURCE_STATE_COMMON,
                ConvertImageState(barrier.m_dstAccess, myQueue, barrier.m_dstStage));
            RHI::CommandList::SetResourceState(*barrier.m_image,
                RHI::ResourceState{ barrier.m_dstAccess, myQueue, barrier.m_dstStage });
        }
    }

    void CommandList::QueueBarrier(const RHI::DeviceMemoryBarrier& barrier)
    {
        D3D12_RESOURCE_ALIASING_BARRIER dx12Barrier{};

        if (barrier.m_resourceBefore)
        {
            switch (barrier.m_typeBefore)
            {
                case RHI::BarrierResourceType::Buffer:
                {
                    auto buffer = static_cast<Buffer*>(barrier.m_resourceBefore);
                    dx12Barrier.pResourceBefore = buffer->GetMemoryView().GetMemory();
                    break;
                }
                case RHI::BarrierResourceType::Image:
                {
                    auto image = static_cast<Image*>(barrier.m_resourceBefore);
                    dx12Barrier.pResourceBefore = image->GetMemoryView().GetMemory();
                    break;
                }
            }
        }

        switch (barrier.m_typeAfter)
        {
            case RHI::BarrierResourceType::Buffer:
            {
                auto buffer = static_cast<Buffer*>(barrier.m_resourceAfter);
                dx12Barrier.pResourceAfter = buffer->GetMemoryView().GetMemory();
                break;
            }
            case RHI::BarrierResourceType::Image:
            {
                auto image = static_cast<Image*>(barrier.m_resourceAfter);
                dx12Barrier.pResourceAfter = image->GetMemoryView().GetMemory();
                break;
            }
            default:
                ASSERT(false, "Invalid memory barrier type.");
        }

        CommandListBase::QueueAliasingBarrier(dx12Barrier, nullptr);
    }

    void CommandList::FlushBarriers()
    {
        CommandListBase::FlushBarriers();
    }

    void CommandList::SetFragmentShadingRate(
        RHI::ShadingRate rate, const RHI::ShadingRateCombinators& combinators)
    {
        if (!CheckBitsAll(GetDevice().GetFeatures().m_shadingRateTypeMask, RHI::ShadingRateTypeFlags::PerDraw))
        {
            ASSERT(false, "Per Draw shading rate is not supported on this platform");
            return;
        }

        m_state.m_shadingRateState.Set(rate, combinators);
    }

    void CommandList::SetStencilRef(uint8_t stencilRef)
    {
        if (m_state.m_stencilRef != stencilRef)
        {
            GetCommandList()->OMSetStencilRef(stencilRef);
            m_state.m_stencilRef = stencilRef;
        }
    }

    void CommandList::SetTopology(RHI::PrimitiveTopology topology)
    {
        if (m_state.m_topology != topology)
        {
            GetCommandList()->IASetPrimitiveTopology(ConvertTopology(topology));
            m_state.m_topology = topology;
        }
    }

    void CommandList::CommitViewportState()
    {
        // [TODO] remove dirty check
        if (!m_state.m_viewportState.m_isDirty)
        {
            return;
        }

        D3D12_VIEWPORT dx12Viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

        const auto& viewports = m_state.m_viewportState.m_states;
        for (uint32_t i = 0; i < viewports.size(); ++i)
        {
            dx12Viewports[i].TopLeftX = viewports[i].m_minX;
            dx12Viewports[i].TopLeftY = viewports[i].m_minY;
            dx12Viewports[i].Width = viewports[i].m_maxX - viewports[i].m_minX;
            dx12Viewports[i].Height = viewports[i].m_maxY - viewports[i].m_minY;
            dx12Viewports[i].MinDepth = viewports[i].m_minZ;
            dx12Viewports[i].MaxDepth = viewports[i].m_maxZ;
        }

        GetCommandList()->RSSetViewports(viewports.size(), dx12Viewports);
        m_state.m_viewportState.m_isDirty = false;
    }

    void CommandList::CommitScissorState()
    {
        if (!m_state.m_scissorState.m_isDirty)
        {
            return;
        }

        D3D12_RECT dx12Scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

        const auto& scissors = m_state.m_scissorState.m_states;
        for (uint32_t i = 0; i < scissors.size(); ++i)
        {
            dx12Scissors[i].left = scissors[i].m_minX;
            dx12Scissors[i].top = scissors[i].m_minY;
            dx12Scissors[i].right = scissors[i].m_maxX;
            dx12Scissors[i].bottom = scissors[i].m_maxY;
        }

        GetCommandList()->RSSetScissorRects(scissors.size(), dx12Scissors);
        m_state.m_scissorState.m_isDirty = false;
    }

    void CommandList::CommitShadingRateState()
    {
        if (!m_state.m_shadingRateState.m_isDirty)
        {
            return;
        }
        ASSERT(
            CheckBitsAll(GetDevice().GetFeatures().m_shadingRateTypeMask, RHI::ShadingRateTypeFlags::PerDraw),
            "PerDraw shading rate is not supported on this platform");

        eastl::array<D3D12_SHADING_RATE_COMBINER, 2> d3d12Combinators;
        for (int i = 0; i < m_state.m_shadingRateState.m_shadingRateCombinators.size(); ++i)
        {
            d3d12Combinators[i] = ConvertShadingRateCombiner(m_state.m_shadingRateState.m_shadingRateCombinators[i]);
        }

        ComPtr<ID3D12GraphicsCommandList5> commandList5;
        GetCommandList()->QueryInterface(IID_PPV_ARGS(commandList5.GetAddressOf()));
        ASSERT(commandList5, "Failed to cast command list to ID3D12GraphicsCommandList5");
        if (commandList5)
        {
            commandList5->RSSetShadingRate(
                ConvertShadingRateEnum(m_state.m_shadingRateState.m_shadingRate), d3d12Combinators.data());
        }
        m_state.m_shadingRateState.m_isDirty = false;
    }

    void CommandList::ExecuteIndirect(const RHI::IndirectArguments& arguments)
    {
        const IndirectBufferSignature* signature = static_cast<const IndirectBufferSignature*>(arguments.m_indirectBufferView->GetSignature());

        const Buffer* buffer = static_cast<const Buffer*>(arguments.m_indirectBufferView->GetBuffer());
        const Buffer* countBuffer = arguments.m_countBuffer ? static_cast<const Buffer*>(arguments.m_countBuffer) : nullptr;
        GetCommandList()->ExecuteIndirect(
            signature->Get(),
            arguments.m_maxSequenceCount,
            buffer->GetMemoryView().GetMemory(),
            buffer->GetMemoryView().GetOffset() + arguments.m_indirectBufferView->GetByteOffset() + arguments.m_indirectBufferByteOffset,
            countBuffer ? countBuffer->GetMemoryView().GetMemory() : nullptr,
            countBuffer ? arguments.m_countBufferByteOffset : 0
        );
    }

    void CommandList::SetIndexBuffer(const RHI::IndexBufferView& indexBufferView)
    {
        uint64_t indexBufferHash = static_cast<uint64_t>(indexBufferView.GetHash());
        if (indexBufferHash != m_state.m_indexBufferHash)
        {
            m_state.m_indexBufferHash = indexBufferHash;
            if (const Buffer* indexBuffer = static_cast<const Buffer*>(indexBufferView.GetBuffer()))
            {
                D3D12_INDEX_BUFFER_VIEW view;
                view.BufferLocation = indexBuffer->GetMemoryView().GetGpuAddress() + indexBufferView.GetByteOffset();
                view.Format = (indexBufferView.GetIndexFormat() == RHI::IndexFormat::UINT16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
                view.SizeInBytes = indexBufferView.GetByteCount();

                GetCommandList()->IASetIndexBuffer(&view);
            }
        }
    }

    void CommandList::SetVertexBuffers(const RHI::VertexBufferView& bufferView)
    {
        bool needsBinding = false;

        for (const RHI::VertexInput& vertexInput : bufferView.GetVertexInputs())
        {
            if (m_state.m_streamBufferHashes[vertexInput.m_inputSlot] != vertexInput.m_vertexInputView.GetHash())
            {
                m_state.m_streamBufferHashes[vertexInput.m_inputSlot] = vertexInput.m_vertexInputView.GetHash();
                needsBinding = true;
            }
        }

        if (needsBinding)
        {
            D3D12_VERTEX_BUFFER_VIEW views[RHI::Limits::Pipeline::StreamCountMax];

            for (const RHI::VertexInput& vertexInput : bufferView.GetVertexInputs())
            {
                const Buffer* buffer = static_cast<const Buffer*>(vertexInput.m_vertexInputView.GetBuffer());
                if (buffer)
                {
                    views[vertexInput.m_inputSlot].BufferLocation = buffer->GetMemoryView().GetGpuAddress() + vertexInput.m_vertexInputView.GetByteOffset();
                    views[vertexInput.m_inputSlot].SizeInBytes = vertexInput.m_vertexInputView.GetByteCount();
                    views[vertexInput.m_inputSlot].StrideInBytes = vertexInput.m_vertexInputView.GetByteStride();
                }
                else
                {
                    views[vertexInput.m_inputSlot] = {};
                }
            }

            GetCommandList()->IASetVertexBuffers(0, bufferView.GetVertexInputs().size(), views);
        }
    }

    // Resolves the effective format of an image view: override if set, otherwise the image's format.
    static RHI::Format ResolveImageViewFormat(const ImageView* view)
    {
        const RHI::Format overrideFormat = view->GetDescriptor().m_overrideFormat;
        return overrideFormat != RHI::Format::Unknown
            ? overrideFormat
            : static_cast<const RHI::Image&>(view->GetResource()).GetDescriptor().m_format;
    }

    // Fills an MSAA-resolve EndingAccess using the color attachment's source / resolve views.
    // The subresource parameter block is kept alive via `subresourceStorage`, which must
    // outlive the pending BeginRenderPass call (CommandList stores it as a member).
    static void FillResolveEndingAccess(
        const ImageView& srcView,
        const ImageView& dstView,
        RHI::Format format,
        bool preserveSource,
        D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS& subresourceStorage,
        D3D12_RENDER_PASS_ENDING_ACCESS& outAccess)
    {
        const auto& srcImageDesc = srcView.GetImage().GetDescriptor();
        const auto& srcViewDesc = srcView.GetDescriptor();
        const auto& dstViewDesc = dstView.GetDescriptor();

        subresourceStorage = {};
        subresourceStorage.SrcSubresource = srcViewDesc.m_mipSliceMin;
        subresourceStorage.DstSubresource = dstViewDesc.m_mipSliceMin;
        subresourceStorage.DstX = 0;
        subresourceStorage.DstY = 0;
        subresourceStorage.SrcRect = {
            0, 0,
            static_cast<LONG>(srcImageDesc.m_size.m_width),
            static_cast<LONG>(srcImageDesc.m_size.m_height)
        };

        outAccess = {};
        outAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
        auto& r = outAccess.Resolve;
        r.pSrcResource           = srcView.GetMemory();
        r.pDstResource           = dstView.GetMemory();
        r.SubresourceCount       = 1;
        r.pSubresourceParameters = &subresourceStorage;
        r.Format                 = ConvertFormat(format);
        r.ResolveMode            = D3D12_RESOLVE_MODE_AVERAGE;
        r.PreserveResolveSource  = preserveSource ? TRUE : FALSE;
    }

    void CommandList::BeginRenderPass(const RHI::RenderPassBeginInfo& info)
    {
        Device& device = static_cast<Device&>(GetDevice());
        auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        // --- Color attachments -------------------------------------------------
        D3D12_RENDER_PASS_RENDER_TARGET_DESC renderTargets[RHI::Limits::Pipeline::AttachmentColorCountMax] = {};
        for (uint32_t i = 0; i < info.m_colorAttachmentCount; ++i)
        {
            const auto& colorAttachment = info.m_colorAttachments[i];
            const auto* view = static_cast<const ImageView*>(colorAttachment.m_view);
            ASSERT(view, "Color attachment {} has a null view", i);

            const RHI::Format format = ResolveImageViewFormat(view);

            renderTargets[i].cpuDescriptor    = descriptorContext.GetCpuNativeHandle(view->GetColorDescriptor());
            renderTargets[i].BeginningAccess  = ConvertBeginningAccess(format, colorAttachment.m_loadStoreAction);

            if (colorAttachment.m_resolveView)
            {
                const auto* resolveView = static_cast<const ImageView*>(colorAttachment.m_resolveView);
                const bool preserveSource =
                    colorAttachment.m_loadStoreAction.m_storeAction == RHI::AttachmentStoreAction::Store;
                FillResolveEndingAccess(
                    *view, *resolveView, format, preserveSource,
                    m_resolveSubresourceParams[i], renderTargets[i].EndingAccess);
            }
            else
            {
                renderTargets[i].EndingAccess = ConvertEndingAccess(colorAttachment.m_loadStoreAction);
            }
        }

        // --- Depth-stencil attachment (optional) ------------------------------
        D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depthStencil {};
        D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* pDepthStencil = nullptr;
        D3D12_RENDER_PASS_FLAGS renderPassFlags = D3D12_RENDER_PASS_FLAG_NONE;

        const auto* dsView = static_cast<const ImageView*>(info.m_depthStencilAttachment.m_view);
        if (dsView)
        {
            const RHI::AttachmentAccess dsAccess = info.m_depthStencilAttachment.m_access;
            const bool readOnlyDsv =
                CheckBitsAny(dsAccess, RHI::AttachmentAccess::Read) &&
                !CheckBitsAny(dsAccess, RHI::AttachmentAccess::Write);
            const DescriptorHandle dsvHandle =
                readOnlyDsv ? dsView->GetDepthStencilReadDescriptor()
                            : dsView->GetDepthStencilDescriptor();

            const RHI::Format dsFormat = ResolveImageViewFormat(dsView);
            const auto& dsLoadStore = info.m_depthStencilAttachment.m_loadStoreAction;

            depthStencil.cpuDescriptor          = descriptorContext.GetCpuNativeHandle(dsvHandle);
            depthStencil.DepthBeginningAccess   = ConvertBeginningAccess(dsFormat, dsLoadStore);
            depthStencil.DepthEndingAccess      = ConvertEndingAccess(dsLoadStore);
            depthStencil.StencilBeginningAccess = ConvertBeginningAccessStencil(dsFormat, dsLoadStore);
            depthStencil.StencilEndingAccess    = ConvertEndingAccessStencil(dsLoadStore);
            pDepthStencil = &depthStencil;

            const bool stencilBeginNoAccess =
                depthStencil.StencilBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
            const bool stencilEndNoAccess =
                depthStencil.StencilEndingAccess.Type == D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;

            if (readOnlyDsv)
            {
                renderPassFlags |= D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_DEPTH;
                if (!stencilBeginNoAccess)
                {
                    renderPassFlags |= D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_STENCIL;
                }
            }

            if (RHI::Validation::isEnabled)
            {
                ASSERT(
                    stencilBeginNoAccess == stencilEndNoAccess,
                    "[CommandList] Depth-stencil attachment has mismatched stencil NO_ACCESS: "
                    "begin and end stencil actions must both be None or both non-None. "
                    "Fix the attachment's stencil load/store actions in the render graph.");
            }

            SetSamplePositions(dsView->GetImage().GetDescriptor().m_multisampleState);
        }
        else if (info.m_colorAttachmentCount > 0)
        {
            const auto* firstColor = static_cast<const ImageView*>(info.m_colorAttachments[0].m_view);
            SetSamplePositions(firstColor->GetImage().GetDescriptor().m_multisampleState);
        }

        // --- Shading rate image (optional, needs ID3D12GraphicsCommandList5) --
        const auto* shadingRateView = static_cast<const ImageView*>(info.m_shadingRateAttachment);
        if (m_state.m_shadingRateImage != shadingRateView &&
            CheckBitsAll(GetDevice().GetFeatures().m_shadingRateTypeMask, RHI::ShadingRateTypeFlags::PerRegion))
        {
            ComPtr<ID3D12GraphicsCommandList5> commandList5;
            GetCommandList()->QueryInterface(IID_PPV_ARGS(commandList5.GetAddressOf()));
            ASSERT(commandList5, "Failed to cast command list to ID3D12GraphicsCommandList5");
            if (commandList5)
            {
                if (shadingRateView)
                {
                    commandList5->RSSetShadingRateImage(shadingRateView->GetMemory());
                    SetFragmentShadingRate(
                        RHI::ShadingRate::Rate1x1,
                        RHI::ShadingRateCombinators{ RHI::ShadingRateCombinerOp::Passthrough, RHI::ShadingRateCombinerOp::Override });
                }
                else
                {
                    commandList5->RSSetShadingRateImage(nullptr);
                    SetFragmentShadingRate(
                        RHI::ShadingRate::Rate1x1,
                        RHI::ShadingRateCombinators{ RHI::ShadingRateCombinerOp::Override, RHI::ShadingRateCombinerOp::Passthrough });
                }
                m_state.m_shadingRateImage = shadingRateView;
            }
        }

        GetCommandList()->BeginRenderPass(
            info.m_colorAttachmentCount, renderTargets, pDepthStencil, renderPassFlags);
    }

    void CommandList::EndRenderPass()
    {
        GetCommandList()->EndRenderPass();
    }

    void CommandList::ClearRenderTarget(const RHI::ImageClearRequest& request)
    {
        Device& device = static_cast<Device&>(GetDevice());
        auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        if (request.m_clearValue.m_type == RHI::ClearValueType::Vector4Float)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
                descriptorContext.GetCpuNativeHandle(static_cast<const ImageView*>(request.m_imageView)->GetColorDescriptor());

            GetCommandList()->ClearRenderTargetView(
                descriptorHandle,
                request.m_clearValue.m_vector4Float.data(),
                0, nullptr);
        }
        else if (request.m_clearValue.m_type == RHI::ClearValueType::DepthStencil)
        {
            // Need to set the custom MSAA positions (if being used) before clearing it.
            SetSamplePositions(request.m_imageView->GetImage().GetDescriptor().m_multisampleState);
            D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle =
                descriptorContext.GetCpuNativeHandle(static_cast<const ImageView*>(request.m_imageView)->GetDepthStencilDescriptor());

            GetCommandList()->ClearDepthStencilView(
                descriptorHandle,
                ConvertDepthStencilClearFlags(request.m_depthStencilClearFlags),
                request.m_clearValue.m_depthStencil.m_depth,
                request.m_clearValue.m_depthStencil.m_stencil,
                0, nullptr);
        }
        else
        {
            ASSERT(false, "Invalid clear value for output merger clear.");
        }
    }

    void CommandList::ClearUnorderedAccess(const RHI::ImageClearRequest& request)
    {
        Device& device = static_cast<Device&>(GetDevice());
        auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        const ImageView& imageView = *static_cast<const ImageView*>(request.m_imageView);
        if (request.m_clearValue.m_type == RHI::ClearValueType::Vector4Uint)
        {
            GetCommandList()->ClearUnorderedAccessViewUint(
                descriptorContext.GetGpuNativeHandle(imageView.GetClearDescriptor()),
                descriptorContext.GetCpuNativeHandle(imageView.GetReadWriteDescriptor()),
                imageView.GetMemory(),
                request.m_clearValue.m_vector4Uint.data(), 0, nullptr);
        }
        else if (request.m_clearValue.m_type == RHI::ClearValueType::Vector4Float)
        {
            GetCommandList()->ClearUnorderedAccessViewFloat(
                descriptorContext.GetGpuNativeHandle(imageView.GetClearDescriptor()),
                descriptorContext.GetCpuNativeHandle(imageView.GetReadWriteDescriptor()),
                imageView.GetMemory(),
                request.m_clearValue.m_vector4Float.data(), 0, nullptr);
        }
        else
        {
            ASSERT(false, "Invalid clear value for image UAV clear.");
        }
    }

    void CommandList::DiscardImage(const RHI::Image& image)
    {
        const Image& dxImage = static_cast<const Image&>(image);
        GetCommandList()->DiscardResource(dxImage.GetMemoryView().GetMemory(), nullptr);
    }

    void CommandList::ClearUnorderedAccess(const RHI::BufferClearRequest& request)
    {
        Device& device = static_cast<Device&>(GetDevice());
        auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        const BufferView& bufferView = *static_cast<const BufferView*>(request.m_bufferView);
        if (request.m_clearValue.m_type == RHI::ClearValueType::Vector4Uint)
        {
            GetCommandList()->ClearUnorderedAccessViewUint(
                descriptorContext.GetGpuNativeHandle(bufferView.GetClearDescriptor()),
                descriptorContext.GetCpuNativeHandle(bufferView.GetReadWriteDescriptor()),
                bufferView.GetMemory(),
                request.m_clearValue.m_vector4Uint.data(), 0, nullptr);
        }
        else if (request.m_clearValue.m_type == RHI::ClearValueType::Vector4Float)
        {
            GetCommandList()->ClearUnorderedAccessViewFloat(
                descriptorContext.GetGpuNativeHandle(bufferView.GetClearDescriptor()),
                descriptorContext.GetCpuNativeHandle(bufferView.GetReadWriteDescriptor()),
                bufferView.GetMemory(),
                request.m_clearValue.m_vector4Float.data(), 0, nullptr);
        }
        else
        {
            ASSERT(false, "Invalid clear value for buffer");
        }
    }

}