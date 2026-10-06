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
 *  -- CommitShaderResources removed; the two-stage assign-then-pull of SRGs is gone.
 *  -- BindShaderInputsForDraw/Dispatch: direct binding via b->GetSpaceId() →
 *     pipelineLayout->FindSpaceIndexBySpaceId() → m_bindingsBySpace dedup → DX12 bind.
 *  -- SetPipelineState: dedup on PSO identity, root signature update on layout change,
 *     binding cache invalidation with it.
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
        // Everything below is a function of the PSO alone — topology and sample positions
        // come out of its own descriptor, and the root signature block keys on its layout —
        // so re-binding the same one is a no-op. The render layer calls this once per submit
        // batch (per view, and per variant once those land), which is why the dedup matters:
        // the same pass PSO is otherwise re-issued for every batch it covers.
        //
        // Pointer identity is enough: Reset() wipes m_state when the list is acquired, so a
        // PSO cannot be freed and another land at the same address within one recording.
        if (m_state.m_pipelineState == &pipelineState)
        {
            return;
        }

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

    void CommandList::SetRootConstants(const uint8_t* data, uint32_t byteCount, uint32_t byteOffset)
    {
        ASSERT(m_state.m_pipelineState, "[CommandList] SetPipelineState must be called before SetRootConstants.");
        const PipelineState&  pso            = static_cast<const PipelineState&>(*m_state.m_pipelineState);
        const PipelineLayout* pipelineLayout = pso.GetPipelineLayout();
        ASSERT(pipelineLayout->HasRootConstants(), "[CommandList] The pipeline state declares no root constants.");
        ASSERT(byteOffset % 4 == 0 && byteCount % 4 == 0
            && byteOffset + byteCount <= pipelineLayout->GetRootConstantsByteCount(),
            "[CommandList] root constant bytes [{}, {}): both ends must be multiples of 4, within the layout's {}.",
            byteOffset, byteOffset + byteCount, pipelineLayout->GetRootConstantsByteCount());

        const RootParameterIndex index = pipelineLayout->GetRootConstantsRootParameterIndex();
        if (pso.GetType() == RHI::PipelineStateType::Dispatch)
        {
            GetCommandList()->SetComputeRoot32BitConstants(index, byteCount / 4, data, byteOffset / 4);
        }
        else
        {
            GetCommandList()->SetGraphicsRoot32BitConstants(index, byteCount / 4, data, byteOffset / 4);
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

        switch (dispatchItem.m_arguments.m_type)
        {
        case RHI::DispatchType::Direct:
        {
            const auto& directArguments = dispatchItem.m_arguments.m_direct;
            GetCommandList()->Dispatch(directArguments.GetNumberOfGroupsX(), directArguments.GetNumberOfGroupsY(), directArguments.GetNumberOfGroupsZ());
            break;
        }
        case RHI::DispatchType::Indirect:
        {
            const RHI::DispatchIndirect& indirect = dispatchItem.m_arguments.m_indirect;

            RHI::IndirectArguments arguments;
            arguments.m_buffer     = indirect.m_buffer;
            arguments.m_byteOffset = indirect.m_byteOffset;
            arguments.m_maxCount   = 1;
            ExecuteIndirect(IndirectCommandType::Dispatch, arguments);
            break;
        }
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
                ExecuteIndirect(IndirectCommandType::Draw, drawItem.m_drawArguments.m_indirect.m_arguments);
                break;
            }
            case RHI::DrawType::IndexedIndirect:
            {
                ASSERT(drawItem.m_indexBufferView.GetBuffer(), "Index buffer view is null!");
                SetIndexBuffer(drawItem.m_indexBufferView);

                ExecuteIndirect(IndirectCommandType::DrawIndexed, drawItem.m_drawArguments.m_indexedIndirect.m_arguments);
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

    namespace
    {
        D3D12_BARRIER_SUBRESOURCE_RANGE ConvertBarrierSubresourceRange(
            const RHI::Image& image, const RHI::ImageSubresourceRange& range)
        {
            const RHI::ImageSubresourceStates& states = image.GetSubresourceStates();

            D3D12_BARRIER_SUBRESOURCE_RANGE converted{};
            if (states.IsWholeImage(range))
            {
                // NumMipLevels 0: IndexOrFirstMipLevel is a subresource index, this one all of them.
                converted.IndexOrFirstMipLevel = 0xFFFFFFFF;
                return converted;
            }

            const RHI::ImageSubresourceRange normalized = states.Normalize(range);
            converted.IndexOrFirstMipLevel = normalized.m_mipSliceMin;
            converted.NumMipLevels         = normalized.m_mipSliceMax - normalized.m_mipSliceMin + 1u;
            converted.FirstArraySlice      = normalized.m_arraySliceMin;
            converted.NumArraySlices       = normalized.m_arraySliceMax - normalized.m_arraySliceMin + 1u;
            if (normalized.m_aspectFlags == image.GetAspectFlags())
            {
                // A color aspect is every plane of a planar format.
                converted.FirstPlane = 0;
                converted.NumPlanes  = GetFormatPlaneCount(image.GetDescriptor().m_format);
            }
            else
            {
                converted.FirstPlane = CheckBitsAny(normalized.m_aspectFlags, RHI::ImageAspectFlags::Depth) ? 0 : 1;
                converted.NumPlanes  = 1;
            }
            return converted;
        }

        //! Render-target and depth-stencil output is ordered in submission order, so a barrier
        //! that changes neither layout nor access between such accesses has nothing to do.
        bool IsRasterOrderedNoOp(const D3D12_TEXTURE_BARRIER& b)
        {
            constexpr D3D12_BARRIER_ACCESS rasterOrdered = D3D12_BARRIER_ACCESS_RENDER_TARGET
                | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
            return b.LayoutBefore == b.LayoutAfter
                && b.AccessBefore == b.AccessAfter
                && (b.AccessBefore & ~rasterOrdered) == D3D12_BARRIER_ACCESS_COMMON;
        }

        //! The accesses beside Undefined are the memory's previous ones, possibly another
        //! resource type's (an aliased resource's previous occupant): a global barrier waits
        //! for them and flushes their writes.
        D3D12_GLOBAL_BARRIER MakePreviousUseBarrier(
            RHI::AccessFlags previous, RHI::AttachmentStage previousStage,
            RHI::AccessFlags dst, RHI::AttachmentStage dstStage, RHI::HardwareQueueClass queue)
        {
            D3D12_GLOBAL_BARRIER g{};
            g.SyncBefore   = ConvertBarrierSync(previousStage, previous, queue);
            g.AccessBefore = ConvertGlobalBarrierAccess(previous);
            g.SyncAfter    = ConvertBarrierSync(dstStage, dst, queue);
            g.AccessAfter  = ConvertGlobalBarrierAccess(dst);
            return g;
        }
    }

    void CommandList::QueueBarrier(const RHI::BufferBarrier& barrier)
    {
        // Cross-queue ownership transfer (Vulkan QFOT) maps to a COMMON bridge in
        // DX12: the release side ends in NO_ACCESS (and the COMMON layout for images),
        // the acquire side starts from there. COMMON is the only layout every queue can
        // use, so the bridge is safe regardless of dstAccess's queue affinity.
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
            // Upload / readback heaps are CPU-visible and never transition.
            return;
        }

        Buffer& buffer = static_cast<Buffer&>(*barrier.m_buffer);

        const bool release = isCrossQueue && myQueue == barrier.m_srcQueue;
        const bool acquire = isCrossQueue && myQueue == barrier.m_dstQueue;

        if (buffer.GetMemoryView().GetType() == BufferMemoryType::Shared)
        {
            // A part of a resource other buffers have parts of takes no native barrier: one
            // spans the whole resource. A handoff between queues has nothing to do here, D3D12
            // having no ownership; within a queue only reads can go without.
            if (RHI::Validation::isEnabled)
            {
                ASSERT(isCrossQueue || !(RHI::HasWrite(barrier.m_srcAccess) || RHI::HasWrite(barrier.m_dstAccess)),
                    "[CommandList] Buffer {} is a part of a shared resource and cannot take a barrier around a write "
                    "(0x{:x} -> 0x{:x} on queue {}). Order its writes and reads by submissions.",
                    barrier.m_buffer->GetName().GetCStr(),
                    static_cast<uint32_t>(barrier.m_srcAccess),
                    static_cast<uint32_t>(barrier.m_dstAccess),
                    static_cast<uint32_t>(myQueue));
            }
        }
        else if (CheckBitsAny(barrier.m_srcAccess, RHI::AccessFlags::Undefined))
        {
            // A buffer has no layout to discard: all there is to it is the memory's previous use.
            ASSERT(!release, "Releasing a buffer from Undefined.");
            const RHI::AccessFlags previous = barrier.m_srcAccess & ~RHI::AccessFlags::Undefined;
            if (!acquire && previous != RHI::AccessFlags::None)
            {
                CommandListBase::QueueGlobalBarrier(MakePreviousUseBarrier(
                    previous, barrier.m_srcStage, barrier.m_dstAccess, barrier.m_dstStage, myQueue));
            }
        }
        else
        {
            D3D12_BUFFER_BARRIER b{};
            b.SyncBefore   = D3D12_BARRIER_SYNC_NONE;
            b.SyncAfter    = D3D12_BARRIER_SYNC_NONE;
            b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
            b.AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS;
            b.pResource    = buffer.GetMemoryView().GetMemory();
            b.Offset       = 0;
            b.Size         = UINT64_MAX;
            if (!acquire)
            {
                b.SyncBefore   = ConvertBarrierSync(barrier.m_srcStage, barrier.m_srcAccess, myQueue);
                b.AccessBefore = ConvertBufferBarrierAccess(barrier.m_srcAccess);
            }
            if (!release)
            {
                b.SyncAfter   = ConvertBarrierSync(barrier.m_dstStage, barrier.m_dstAccess, myQueue);
                b.AccessAfter = ConvertBufferBarrierAccess(barrier.m_dstAccess);
            }
            CommandListBase::QueueBufferBarrier(b);
        }

        // The release half deliberately does NOT touch the tracked ResourceState — see
        // the ImageBarrier overload for the rationale.
        if (!release)
        {
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

        D3D12_TEXTURE_BARRIER b{};
        b.SyncBefore   = D3D12_BARRIER_SYNC_NONE;
        b.SyncAfter    = D3D12_BARRIER_SYNC_NONE;
        b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        b.AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS;
        b.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
        b.LayoutAfter  = D3D12_BARRIER_LAYOUT_COMMON;
        b.pResource    = image.GetMemoryView().GetMemory();
        b.Subresources = ConvertBarrierSubresourceRange(image, barrier.m_range);
        b.Flags        = D3D12_TEXTURE_BARRIER_FLAG_NONE;

        const bool release = isCrossQueue && myQueue == barrier.m_srcQueue;
        const bool acquire = isCrossQueue && myQueue == barrier.m_dstQueue;
        const bool discard = CheckBitsAny(barrier.m_srcAccess, RHI::AccessFlags::Undefined);
        ASSERT(!discard || myQueue != RHI::HardwareQueueClass::Copy,
            "Image barrier from Undefined on the Copy queue, which cannot transition layouts.");
        if (!acquire)
        {
            b.SyncBefore   = ConvertBarrierSync(barrier.m_srcStage, barrier.m_srcAccess, myQueue);
            b.AccessBefore = ConvertImageBarrierAccess(barrier.m_srcAccess);
            b.LayoutBefore = ConvertBarrierLayout(barrier.m_srcAccess, myQueue);
        }
        else if (discard)
        {
            b.LayoutBefore = D3D12_BARRIER_LAYOUT_UNDEFINED;
        }
        if (!release)
        {
            b.SyncAfter   = ConvertBarrierSync(barrier.m_dstStage, barrier.m_dstAccess, myQueue);
            b.AccessAfter = ConvertImageBarrierAccess(barrier.m_dstAccess);
            b.LayoutAfter = ConvertBarrierLayout(barrier.m_dstAccess, myQueue);
        }
        if (discard)
        {
            // Initializes render-target / depth-stencil compression metadata.
            if (CheckBitsAny(image.GetDescriptor().m_bindFlags, RHI::ImageBindFlags::Color | RHI::ImageBindFlags::DepthStencil))
            {
                b.Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
            }
            const RHI::AccessFlags previous = barrier.m_srcAccess & ~RHI::AccessFlags::Undefined;
            if (!acquire && previous != RHI::AccessFlags::None)
            {
                CommandListBase::QueueGlobalBarrier(MakePreviousUseBarrier(
                    previous, barrier.m_srcStage, barrier.m_dstAccess, barrier.m_dstStage, myQueue));
            }
        }
        if (!IsRasterOrderedNoOp(b))
        {
            CommandListBase::QueueTextureBarrier(b);
        }

        // The release half deliberately does NOT touch the tracked ResourceState. The
        // release runs on a different queue/thread than the matching acquire (e.g. async
        // upload's Copy queue vs the graphics acquire); writing the transit state here
        // races the acquire and, landing last, would leave the resource parked at
        // {None, srcQueue}. The render-graph compile then re-reads that as "still needs a
        // cross-queue acquire" and re-emits a barrier onto an already-transitioned
        // resource. The destination queue's acquire is the sole authority for the
        // post-handoff state; the source queue the acquire needs for its cross-queue
        // detection is already carried by the pre-release tracked state.
        if (!release)
        {
            RHI::CommandList::SetResourceState(*barrier.m_image, barrier.m_range,
                RHI::ResourceState{ barrier.m_dstAccess, myQueue, barrier.m_dstStage });
        }
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

    void CommandList::ExecuteIndirect(IndirectCommandType type, const RHI::IndirectArguments& arguments)
    {
        const Buffer* buffer      = static_cast<const Buffer*>(arguments.m_buffer);
        const Buffer* countBuffer = static_cast<const Buffer*>(arguments.m_countBuffer);

        if (RHI::Validation::isEnabled)
        {
            ASSERT(buffer, "[CommandList] Indirect call without an argument buffer.");
            ASSERT(
                CheckBitsAll(buffer->GetDescriptor().m_bindFlags, RHI::BufferBindFlags::Indirect),
                "[CommandList] The argument buffer of an indirect call needs BufferBindFlags::Indirect.");
            ASSERT(
                !countBuffer || CheckBitsAll(countBuffer->GetDescriptor().m_bindFlags, RHI::BufferBindFlags::Indirect),
                "[CommandList] The count buffer of an indirect call needs BufferBindFlags::Indirect.");
        }

        Device& device = static_cast<Device&>(GetDevice());
        GetCommandList()->ExecuteIndirect(
            device.GetCommandSignature(type),
            arguments.m_maxCount,
            buffer->GetMemoryView().GetMemory(),
            buffer->GetMemoryView().GetOffset() + arguments.m_byteOffset,
            countBuffer ? countBuffer->GetMemoryView().GetMemory() : nullptr,
            countBuffer ? countBuffer->GetMemoryView().GetOffset() + arguments.m_countByteOffset : 0
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

    // The first subresource an image view covers.
    static UINT GetImageViewSubresource(const ImageView& view)
    {
        const auto& imageDesc = view.GetImage().GetDescriptor();
        const auto& viewDesc  = view.GetDescriptor();
        return D3D12CalcSubresource(
            viewDesc.m_mipSliceMin, viewDesc.m_arraySliceMin, 0, imageDesc.m_mipLevels, imageDesc.m_arraySize);
    }

    // Takes a render target's subresource to the resolve-source layout, or back from it.
    static D3D12_TEXTURE_BARRIER MakeResolveSourceBarrier(ID3D12Resource* resource, UINT subresource, bool toResolveSource)
    {
        D3D12_TEXTURE_BARRIER b{};
        b.SyncBefore   = D3D12_BARRIER_SYNC_RENDER_TARGET;
        b.SyncAfter    = D3D12_BARRIER_SYNC_RESOLVE;
        b.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
        b.AccessAfter  = D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
        b.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        b.LayoutAfter  = D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
        b.pResource    = resource;
        // NumMipLevels 0: IndexOrFirstMipLevel is a subresource index.
        b.Subresources.IndexOrFirstMipLevel = subresource;
        b.Flags        = D3D12_TEXTURE_BARRIER_FLAG_NONE;
        if (!toResolveSource)
        {
            eastl::swap(b.SyncBefore, b.SyncAfter);
            eastl::swap(b.AccessBefore, b.AccessAfter);
            eastl::swap(b.LayoutBefore, b.LayoutAfter);
        }
        return b;
    }

    void CommandList::BeginRenderPass(const RHI::RenderPassBeginInfo& info)
    {
        Device& device = static_cast<Device&>(GetDevice());
        auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        ASSERT(m_pendingResolves.empty(), "[CommandList] BeginRenderPass inside a render pass.");

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
                // EndRenderPass resolves, so the target is kept until then. Not an ending-access
                // resolve: see EndRenderPass.
                const auto* resolveView = static_cast<const ImageView*>(colorAttachment.m_resolveView);
                PendingResolve& resolve = m_pendingResolves.push_back();
                resolve.m_source                 = view->GetMemory();
                resolve.m_destination            = resolveView->GetMemory();
                resolve.m_sourceSubresource      = GetImageViewSubresource(*view);
                resolve.m_destinationSubresource = GetImageViewSubresource(*resolveView);
                resolve.m_format                 = ConvertFormat(format);
                renderTargets[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
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

        if (m_pendingResolves.empty())
        {
            return;
        }

        // No layout admits both render-target and resolve-source access. The sources leave
        // RENDER_TARGET for the resolve and come back to it, so the state the caller tracks
        // for them holds on return.
        for (const PendingResolve& resolve : m_pendingResolves)
        {
            CommandListBase::QueueTextureBarrier(
                MakeResolveSourceBarrier(resolve.m_source, resolve.m_sourceSubresource, true));
        }
        CommandListBase::FlushBarriers();

        for (const PendingResolve& resolve : m_pendingResolves)
        {
            // Not ResolveSubresourceRegion: when it is a command list's only write to a swap
            // chain image, the device is removed (DXGI_ERROR_ACCESS_DENIED).
            GetCommandList()->ResolveSubresource(
                resolve.m_destination, resolve.m_destinationSubresource,
                resolve.m_source, resolve.m_sourceSubresource, resolve.m_format);
        }

        // Flushed here: barriers of one Barrier() call are unordered, and the next to touch a
        // source would otherwise share a call with this one.
        for (const PendingResolve& resolve : m_pendingResolves)
        {
            CommandListBase::QueueTextureBarrier(
                MakeResolveSourceBarrier(resolve.m_source, resolve.m_sourceSubresource, false));
        }
        CommandListBase::FlushBarriers();

        m_pendingResolves.clear();
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