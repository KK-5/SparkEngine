/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandList.h"

#include <Log/SpdLogSystem.h>

#include <D3D12Factory.h>
#include <Conversions.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/ShaderResource/ShaderResource.h>
#include <Resource/Buffer/Buffer.h>
#include <Resource/Image/Image.h>
#include <SwapChain/SwapChain.h>

#include "CommandQueue.h"

namespace Spark::RHI::DX12
{
    void CommandList::Init(Device& device, RHI::HardwareQueueClass hardwareQueueClass, ID3D12CommandAllocator* commandAllocator)
    {
        CommandListBase::Init(device, hardwareQueueClass, commandAllocator);

        if (GetHardwareQueueClass() != RHI::HardwareQueueClass::Copy)
        {
            auto& descriptorContext = Service<D3D12FactoryInterface>::Get()->AcquireDescriptorContext();
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

    void CommandList::Reset(ID3D12CommandAllocator* commandAllocator)
    {
        CommandListBase::Reset(commandAllocator);

        if (GetHardwareQueueClass() != RHI::HardwareQueueClass::Copy)
        {
            auto& descriptorContext = Service<D3D12FactoryInterface>::Get()->AcquireDescriptorContext();
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

    void CommandList::SetShaderResourceForDraw(const RHI::ShaderResource& shaderResourceGroup)
    {
        SetShaderResource<RHI::PipelineStateType::Draw>(static_cast<const ShaderResource*>(&shaderResourceGroup));
    }

    void CommandList::SetShaderResourceForDispatch(const RHI::ShaderResource& shaderResourceGroup)
    {
        SetShaderResource<RHI::PipelineStateType::Dispatch>(static_cast<const ShaderResource*>(&shaderResourceGroup));
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

        if (!CommitShaderResources<RHI::PipelineStateType::Dispatch>(dispatchItem))
        {
            LOG_WARN("[CommandList] Failed to bind shader resources for dispatch item. Skipping.");
            return;
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

        if (!CommitShaderResources<RHI::PipelineStateType::Draw>(drawItem))
        {
            LOG_WARN("[CommandList] Failed to bind shader resources for draw item. Skipping.");
            return;
        }

        // SetStreamBuffers(*drawItem.m_geometryView, drawItem.m_streamIndices);
        SetStencilRef(drawItem.m_stencilRef);

        RHI::CommandListScissorState scissorState;
        if (drawItem.m_scissorsCount)
        {
            scissorState = m_state.m_scissorState;
            SetScissors(drawItem.m_scissors, drawItem.m_scissorsCount);
        }

        RHI::CommandListViewportState viewportState;
        if (drawItem.m_viewportsCount)
        {
            viewportState = m_state.m_viewportState;
            SetViewports(drawItem.m_viewports, drawItem.m_viewportsCount);
        }

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

            // Restore the scissors if needed.
            if (scissorState.IsValid())
            {
                SetScissors(scissorState.m_states.data(), scissorState.m_states.size());
            }

            // Restore the viewports if needed.
            if (viewportState.IsValid())
            {
                SetViewports(viewportState.m_states.data(), viewportState.m_states.size());
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

    template <RHI::PipelineStateType pipelineType, typename Item>
    bool CommandList::CommitShaderResources(const Item& item)
    {
        ShaderResourceBindings& bindings = GetShaderResourceBindingsByPipelineType(pipelineType);

        const PipelineState* pipelineState = static_cast<const PipelineState*>(item.m_pipelineState);
        if(!pipelineState)
        {
            ASSERT(false, "Pipeline state not provided");
            return false;
        }

        const PipelineLayout* pipelineLayout = pipelineState->GetPipelineLayout();
        if (!pipelineLayout)
        {
            ASSERT(false, "Pipeline layout is null.");
            return false;
        }

        bool updatePipelineState = m_state.m_pipelineState != pipelineState;
        // The pipeline state gets set first.
        if (updatePipelineState)
        {
            if (!pipelineState->IsInitialized())
            {
                LOG_WARN("[CommandList] Pipeline State is not initialized.");
                return false;
            }

            GetCommandList()->SetPipelineState(pipelineState->Get());

            // Check if we need to set custom sample positions
            if constexpr (pipelineType == RHI::PipelineStateType::Draw)
            {
                const auto& pipelineData = pipelineState->GetPipelineStateData();
                auto& multisampleState = pipelineData.m_drawData.m_multisampleState;
                SetSamplePositions(multisampleState);
                SetTopology(pipelineData.m_drawData.m_primitiveTopology);
            }

            // Pipeline layouts change when pipeline states do, just not as often. If the root
            // signature changes all shader bindings are invalidated.
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
                    return false;
                }

                bindings.m_pipelineLayout = pipelineLayout;
                bindings.m_hasRootConstants = pipelineLayout->HasRootConstants();

                // We need to zero these out, since the command list root parameters are invalid.
                for (size_t i = 0; i < bindings.m_srgsByIndex.size(); ++i)
                {
                    bindings.m_srgsByIndex[i] = nullptr;
                }
                bindings.m_bindlessHeapLastIndex = -1;
            }

            m_state.m_pipelineState = pipelineState;
        }

        // Assign shader resource groups from the item to slot bindings.
        for (uint32_t srgIndex = 0; srgIndex < item.m_shaderResourceCount; ++srgIndex)
        {
            SetShaderResource<pipelineType>(static_cast<const ShaderResource*>(item.m_shaderResource[srgIndex]));
        }

        if (item.m_uniqueShaderResource)
        {
            SetShaderResource<pipelineType>(static_cast<const ShaderResource*>(item.m_uniqueShaderResource));
        }

        // Bind the inline constants from the item, if present.
        if (bindings.m_hasRootConstants && item.m_rootConstantSize)
        {
            ASSERT((item.m_rootConstantSize % 4) == 0, "Invalid inline constant data size. It must be a multiple of 32 bit.");
            switch (pipelineType)
            {
            case RHI::PipelineStateType::Draw:
                GetCommandList()->SetGraphicsRoot32BitConstants(0, item.m_rootConstantSize / 4, item.m_rootConstants, 0);
                break;

            case RHI::PipelineStateType::Dispatch:
                GetCommandList()->SetComputeRoot32BitConstants(0, item.m_rootConstantSize / 4, item.m_rootConstants, 0);
                break;

            default:
                ASSERT(false, "Invalid PipelineType");
                return false;
            }
        }

        // Pull from slot bindings dictated by the pipeline layout. Re-bind anything that has changed
        // at the flat index level.
        for (size_t srgIndex = 0; srgIndex < pipelineLayout->GetRootParameterBindingCount(); ++srgIndex)
        {
            const size_t srgSlot = pipelineLayout->GetSlotByIndex(srgIndex);
            const ShaderResource* shaderResource = bindings.m_srgsBySlot[srgSlot];
            RootParameterBinding binding = pipelineLayout->GetRootParameterBindingByIndex(srgIndex);

            // Check if we are iterating over the bindless srg slot
            // [TODO] modify the binding method of bindless
            const auto& device = static_cast<Device&>(GetDevice());
            if (srgSlot == device.GetBindlessSrgSlot() && shaderResource == nullptr)
            {
                // Skip in case the global static heap is already bound
                if (bindings.m_bindlessHeapLastIndex == binding.m_bindlessTable)
                {
                    continue;
                }
                ASSERT(binding.m_bindlessTable.IsValid(), "BindlessSRG handles is not valid.");

                switch (pipelineType)
                {
                case RHI::PipelineStateType::Draw:
                    {
                        GetCommandList()->SetGraphicsRootDescriptorTable(
                            binding.m_bindlessTable, m_descriptorContext->GetBindlessGpuPlatformHandle());
                        break;
                    }
                case RHI::PipelineStateType::Dispatch:
                    {
                        GetCommandList()->SetComputeRootDescriptorTable(
                            binding.m_bindlessTable, m_descriptorContext->GetBindlessGpuPlatformHandle());
                        break;
                    }
                default:
                    ASSERT(false, "Invalid PipelineType");
                    break;
                }
                bindings.m_bindlessHeapLastIndex = binding.m_bindlessTable;
                continue;
            }

            if (RHI::Validation::isEnabled)
            {
                if (!shaderResource)
                {
                    /*
                    eastl::string slotSrgString;
                    for (size_t slot = 0; slot < RHI::Limits::Pipeline::ShaderResourceCountMax; ++slot)
                    {
                        if (bindings.m_srgsBySlot[slot])
                        {
                            if (!slotSrgString.empty())
                            {
                                slotSrgString += ", ";
                            }

                            slotSrgString += AZStd::string::format("Slot #%zu = '%s'", slot, bindings.m_srgsBySlot[slot]->GetName().GetCStr());
                        }
                    }

                    // this assert typically happens when a shader needs a particular Srg (e.g., the ViewSrg) but the code did not bind it,
                    // check the pass code in this callstack to determine why it was not bound
                    AZ_Assert(false, "The DrawItem being submitted doesn't provide an SRG for slot '%zu', which the shader is expecting. If this slot is for a Pass, View or Scene SRG, this likely means "
                        "the pass didn't collect it (for the view SRG, check if your pass provides a PipelineViewTag). The SRGs currently provided by the DrawItem are: %s",
                        srgSlot,
                        slotSrgString.c_str());
                    */
                    return false;
                }
            }

            bool updateSRG = bindings.m_srgsByIndex[srgIndex] != shaderResource;
            if (updateSRG)
            {
                bindings.m_srgsByIndex[srgIndex] = shaderResource;
                const ShaderResourceCompiledData& compiledData = shaderResource->GetCompiledData();

                switch (pipelineType)
                {
                    case RHI::PipelineStateType::Draw:
                    {
                        if (binding.m_resourceTable != InvalidRootParameterIndex && compiledData.m_gpuViewsDescriptorHandle.ptr)
                        {
                            GetCommandList()->SetGraphicsRootDescriptorTable(binding.m_resourceTable.GetIndex(), compiledData.m_gpuViewsDescriptorHandle);
                        }

                        if (binding.m_constantBuffer != InvalidRootParameterIndex)
                        {
                            GetCommandList()->SetGraphicsRootConstantBufferView(binding.m_constantBuffer.GetIndex(), compiledData.m_gpuConstantAddress);
                        }

                        if (binding.m_samplerTable != InvalidRootParameterIndex && compiledData.m_gpuSamplersDescriptorHandle.ptr)
                        {
                            GetCommandList()->SetGraphicsRootDescriptorTable(binding.m_samplerTable.GetIndex(), compiledData.m_gpuSamplersDescriptorHandle);
                        }

                        // [TODO] remove bindless from this
                        for (uint32_t unboundedArrayIndex = 0; unboundedArrayIndex < ShaderResourceCompiledData::MaxUnboundedArrays;
                             ++unboundedArrayIndex)
                        {
                            if (binding.m_bindlessTable != InvalidRootParameterIndex &&
                                compiledData.m_gpuUnboundedArraysDescriptorHandles[unboundedArrayIndex].ptr)
                            {
                                GetCommandList()->SetGraphicsRootDescriptorTable(
                                    binding.m_bindlessTable.GetIndex(),
                                    compiledData.m_gpuUnboundedArraysDescriptorHandles[unboundedArrayIndex]);
                            }
                        }

                        break;
                    }
                    case RHI::PipelineStateType::Dispatch:
                    {
                        if (binding.m_resourceTable != InvalidRootParameterIndex && compiledData.m_gpuViewsDescriptorHandle.ptr)
                        {
                            GetCommandList()->SetComputeRootDescriptorTable(binding.m_resourceTable.GetIndex(), compiledData.m_gpuViewsDescriptorHandle);
                        }

                        if (binding.m_constantBuffer != InvalidRootParameterIndex)
                        {
                            GetCommandList()->SetComputeRootConstantBufferView(binding.m_constantBuffer.GetIndex(), compiledData.m_gpuConstantAddress);
                        }

                        if (binding.m_samplerTable != InvalidRootParameterIndex && compiledData.m_gpuSamplersDescriptorHandle.ptr)
                        {
                            GetCommandList()->SetComputeRootDescriptorTable(binding.m_samplerTable.GetIndex(), compiledData.m_gpuSamplersDescriptorHandle);
                        }

                        // [TODO] remove bindless from this
                        for (uint32_t unboundedArrayIndex = 0; unboundedArrayIndex < ShaderResourceCompiledData::MaxUnboundedArrays;
                             ++unboundedArrayIndex)
                        {
                            if (binding.m_bindlessTable != InvalidRootParameterIndex &&
                                compiledData.m_gpuUnboundedArraysDescriptorHandles[unboundedArrayIndex].ptr)
                            {
                                GetCommandList()->SetComputeRootDescriptorTable(
                                    binding.m_bindlessTable.GetIndex(),
                                    compiledData.m_gpuUnboundedArraysDescriptorHandles[unboundedArrayIndex]);
                            }
                        }
                        break;
                    }
                    default:
                    {
                        ASSERT(false, "Invalid PipelineType");
                        return false;
                    }
                }
            }
        }
    }
}