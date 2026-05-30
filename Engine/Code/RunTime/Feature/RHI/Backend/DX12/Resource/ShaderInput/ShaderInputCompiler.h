#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Resource/ShaderInput/ShaderInputCompiler.h>

#include <Descriptor/Descriptor.h>
#include "ShaderBindings.h"

namespace Spark::RHI::DX12
{
    class Device;

    class ShaderInputCompiler final : public RHI::ShaderInputCompiler
    {
        using Base = RHI::ShaderInputCompiler;

    public:
        ShaderInputCompiler() = default;
        ~ShaderInputCompiler() override = default;

    private:
        static constexpr size_t ViewsFixedSize = 16;

        ResultCode InitInternal(RHI::Device& device, const RHI::ShaderInputCompilerDescriptor& desc) override;
        ResultCode CompileInternal(RHI::ShaderBindings& bindings) override;

        /// Allocate ring-buffered descriptor tables + constant memory on first Compile.
        void AllocateBindings(ShaderBindings& bindings);

        /// Advance ring index, write descriptors for the new frame slot.
        void UpdateBindings(ShaderBindings& bindings);

        /// Write Buffer / Image descriptors into the current frame's resource sub-table.
        void UpdateViewsDescriptorTable(
            DescriptorTable frameTable,
            ShaderBindings& bindings);

        /// Write Sampler descriptors into the current frame's sampler sub-table.
        void UpdateSamplersDescriptorTable(
            DescriptorTable frameTable,
            ShaderBindings& bindings);

        /// memcpy constant data per-register according to ShaderInputGroup::m_constantBuffers.
        void UpdateConstants(ShaderBindings& bindings);

        //////////////////////////////////////////////////////////////////////////
        // Descriptor gather helpers — append (one descriptor per array slot) into a
        // shared per-SpaceGroup source array so the whole sub-table can be updated
        // in a single CopyDescriptors call.

        template <typename TBase, typename TDx12>
        void AppendSRVsFromViews(
            eastl::span<const ConstPtr<TBase>> views,
            D3D12_SRV_DIMENSION dimension,
            eastl::fixed_vector<DescriptorHandle, ViewsFixedSize>& out);

        template <typename TBase, typename TDx12>
        void AppendUAVsFromViews(
            eastl::span<const ConstPtr<TBase>> views,
            D3D12_UAV_DIMENSION dimension,
            eastl::fixed_vector<DescriptorHandle, ViewsFixedSize>& out);

        void AppendCBVsFromBufferViews(
            eastl::span<const ConstPtr<RHI::BufferView>> views,
            eastl::fixed_vector<DescriptorHandle, ViewsFixedSize>& out);

        //////////////////////////////////////////////////////////////////////////
        // Sub-range helpers — sized from SpaceGroup walk, not from a cached stride.

        /// Sum of m_count for all Buffer / Image handles in the space group.
        static uint32_t ComputeViewsPerFrame(
            const RHI::PipelineLayoutDescriptor& desc,
            const RHI::ShaderInputGroup& group);

        /// Sum of m_count for all Sampler handles in the space group.
        static uint32_t ComputeSamplersPerFrame(
            const RHI::PipelineLayoutDescriptor& desc,
            const RHI::ShaderInputGroup& group);
    };
}
