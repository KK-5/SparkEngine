/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Resource/ShaderResource/ShaderResourcePool.h>

namespace Spark::RHI::DX12
{
    class Device;
    class BufferView;
    class ImageView;
    class ShaderResourceGroupLayout;
    class DescriptorContext;

    class ShaderResourcePool final : public RHI::ShaderResourcePool
    {
        using Base = RHI::ShaderResourcePool;
    
    private:
        ShaderResourcePool() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::ShaderResourcePool override
        RHI::ResultCode InitInternal(RHI::Device& deviceBase, const RHI::ShaderResourcePoolDescriptor& descriptor) override;
        RHI::ResultCode InitShaderResourceInternal(RHI::ShaderResource& groupBase) override;
        void ShutdownInternal() override;
        ResultCode CompileShaderReourceBufferInternal(RHI::ShaderResource& shaderResourceBase) override;
        ResultCode CompileShaderReourceImageInternal(RHI::ShaderResource& shaderResourceBase) override;
        ResultCode CompileShaderReourceConstantDataInternal(RHI::ShaderResource& shaderResourceBase) override;
        ResultCode CompileShaderReourceSamplerInternal(RHI::ShaderResource& shaderResourceBase) override;
        void ShutdownResourceInternal(RHI::Resource& resourceBase) override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // FrameEventBus::Handler
        void OnFrameEnd() override;
        //////////////////////////////////////////////////////////////////////////

        void UpdateViewsDescriptorTable(DescriptorTable descriptorTable,
                                        RHI::ShaderResource& group,
                                        bool forceUpdateViews = false);
        void UpdateSamplersDescriptorTable(DescriptorTable descriptorTable, RHI::ShaderResource& group);
        void UpdateUnboundedArrayDescriptorTables(ShaderResource& group);

        //! Update all the buffer views for the unbounded array
        void UpdateUnboundedBuffersDescTable(
            DescriptorTable descriptorTable,
            RHI::ShaderInputIndex shaderInputIndex,
            RHI::ShaderInputBufferAccess bufferAccess);

        //! Update all the image views for the unbounded array
        void UpdateUnboundedImagesDescTable(
            DescriptorTable descriptorTable,
            RHI::ShaderInputIndex shaderInputIndex,
            RHI::ShaderInputImageAccess imageAccess,
            RHI::ShaderInputImageType imageType);

        void UpdateDescriptorTableRange(
            DescriptorTable descriptorTable,
            const eastl::span<DescriptorHandle>& descriptors,
            RHI::ShaderInputIndex bufferInputIndex);

        void UpdateDescriptorTableRange(
            DescriptorTable descriptorTable, const eastl::span<DescriptorHandle>& descriptors, RHI::ShaderInputIndex imageIndex);

        void UpdateDescriptorTableRange(
            DescriptorTable descriptorTable,
            RHI::ShaderInputIndex samplerIndex,
            eastl::span<const RHI::SamplerState> samplerStates);

        //Cache all the gpu handles for the Descriptor tables related to all the views
        void CacheGpuHandlesForViews(ShaderResource& shaderResource);
        
        DescriptorTable GetBufferTable(DescriptorTable descriptorTable, RHI::ShaderInputIndex bufferIndex) const;
        DescriptorTable GetImageTable(DescriptorTable descriptorTable, RHI::ShaderInputIndex imageIndex) const;
        DescriptorTable GetSamplerTable(DescriptorTable descriptorTable, RHI::ShaderInputIndex samplerInputIndex) const;

        template<typename T, typename U>
        void GetSRVsFromImageViews(
            const eastl::span<const ConstPtr<T>>& imageViews,
            D3D12_SRV_DIMENSION dimension,
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result);

        template<typename T, typename U>
        void GetUAVsFromImageViews(
            const eastl::span<const ConstPtr<T>>& bufferViews,
            D3D12_UAV_DIMENSION dimension,
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result);

        void GetCBVsFromBufferViews(
            const eastl::span<const ConstPtr<RHI::BufferView>>& bufferViews,
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result);

        MemoryPoolSubAllocator m_constantAllocator;
        DescriptorContext* m_descriptorContext = nullptr;
        uint32_t m_constantBufferSize = 0;
        uint32_t m_constantBufferRingSize = 0;
        uint32_t m_viewsDescriptorTableSize = 0;
        uint32_t m_viewsDescriptorTableRingSize = 0;
        uint32_t m_samplersDescriptorTableSize = 0;
        uint32_t m_samplersDescriptorTableRingSize = 0;
        uint32_t m_descriptorTableBufferOffset = 0;
        uint32_t m_descriptorTableImageOffset = 0;
        uint32_t m_unboundedArrayCount = 0;

    };
}