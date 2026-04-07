#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Resource/ShaderResource/ShaderResourceCompiler.h>
#include <RHI/Device/DeviceObjectFactory.h>
#include <Descriptor/Descriptor.h>

namespace Spark::RHI::DX12
{
    class Device;
    class BufferView;
    class ImageView;
    class ShaderResource;

    static constexpr size_t SRGViewsFixedSize = 16;

    class ShaderResourceCompiler final : public RHI::ShaderResourceCompiler
    {
        using Base = RHI::ShaderResourceCompiler;
    public:
        ~ShaderResourceCompiler() override = default;
        ShaderResourceCompiler() = default;

    private:
        // friend class DeviceObjectFactory<ShaderResourceCompiler>;
        // friend class ID3D12Factory;

        //////////////////////////////////////////////////////////////////////////
        // RHI::ShaderResourceCompiler overrides
        ResultCode InitInternal(RHI::Device& device, const RHI::ShaderResourceCompilerDescriptor& desc) override;
        ResultCode CompilerInternal(eastl::span<RHI::ShaderResource*> shaderResources) override;
        //////////////////////////////////////////////////////////////////////////

        /// Allocate GPU resources (descriptor tables, constant buffer) for a single SRG.
        /// Called on first compile only. The SRG owns and releases these resources.
        void AllocateShaderResource(RHI::ShaderResource& shaderResource, const RHI::ShaderResourceLayout& layout);

        /// Update descriptor tables and constant data for a single SRG at the current frame.
        void UpdateShaderResource(RHI::ShaderResource& shaderResource);

        //////////////////////////////////////////////////////////////////////////
        // Descriptor table fill helpers

        void UpdateViewsDescriptorTable(
            DescriptorTable frameViewsTable,
            RHI::ShaderResource& shaderResource,
            const RHI::ShaderResourceLayout& layout);

        void UpdateSamplersDescriptorTable(
            DescriptorTable frameSamplersTable,
            RHI::ShaderResource& shaderResource,
            const RHI::ShaderResourceLayout& layout);

        //////////////////////////////////////////////////////////////////////////
        // Sub-table helpers

        DescriptorTable GetBufferTable(
            DescriptorTable descriptorTable,
            RHI::ShaderInputIndex bufferIndex,
            const RHI::ShaderResourceLayout& layout,
            uint32_t bufferOffset) const;

        DescriptorTable GetImageTable(
            DescriptorTable descriptorTable,
            RHI::ShaderInputIndex imageIndex,
            const RHI::ShaderResourceLayout& layout,
            uint32_t imageOffset) const;

        DescriptorTable GetSamplerTable(
            DescriptorTable descriptorTable,
            RHI::ShaderInputIndex samplerInputIndex,
            const RHI::ShaderResourceLayout& layout) const;

        //////////////////////////////////////////////////////////////////////////
        // Descriptor range update helpers

        void UpdateDescriptorTableRangeForBuffer(
            DescriptorTable descriptorTable,
            const eastl::span<DescriptorHandle>& descriptors,
            RHI::ShaderInputIndex bufferInputIndex,
            const RHI::ShaderResourceLayout& layout);

        void UpdateDescriptorTableRangeForImage(
            DescriptorTable descriptorTable,
            const eastl::span<DescriptorHandle>& descriptors,
            RHI::ShaderInputIndex imageInputIndex,
            const RHI::ShaderResourceLayout& layout);

        void UpdateDescriptorTableRange(
            DescriptorTable descriptorTable,
            eastl::span<const RHI::SamplerState> samplerStates,
            RHI::ShaderInputIndex samplerIndex,
            const RHI::ShaderResourceLayout& layout);

        void UpdateDescriptorTableRangeForSampler(
            DescriptorTable descriptorTable,
            const eastl::span<DescriptorHandle>& descriptors,
            RHI::ShaderInputIndex samplerIndex,
            const RHI::ShaderResourceLayout& layout);

        //////////////////////////////////////////////////////////////////////////
        // Descriptor gather helpers

        template<typename T, typename U>
        void GetSRVsFromImageViews(
            const eastl::span<const ConstPtr<T>>& imageViews,
            D3D12_SRV_DIMENSION dimension,
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result);

        template<typename T, typename U>
        void GetUAVsFromImageViews(
            const eastl::span<const ConstPtr<T>>& imageViews,
            D3D12_UAV_DIMENSION dimension,
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result);

        void GetCBVsFromBufferViews(
            const eastl::span<const ConstPtr<RHI::BufferView>>& bufferViews,
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result);

    };
}
