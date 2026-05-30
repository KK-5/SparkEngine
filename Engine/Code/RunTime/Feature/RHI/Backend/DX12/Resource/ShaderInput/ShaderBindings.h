#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <DX12.h>
#include <MemoryView.h>
#include <Descriptor/Descriptor.h>

namespace Spark::RHI::DX12
{
    class Device;
    class Sampler;
    class ShaderInputCompiler;
    class CommandList;

    /// Per-frame GPU entry points filled by ShaderInputCompiler at allocation time
    /// (handles + constant addresses) and consumed by CommandList at bind time.
    /// m_gpuConstantAddresses / m_cpuConstantAddresses are indexed in parallel with
    /// the source-of-truth RHI::ShaderInputGroup::m_constantBuffers — the k-th entry
    /// is the k-th unique cbuffer register in the SpaceGroup. The k-th DX12 root index
    /// (SpaceCBVBinding::m_rootIndices[k]) targets the same CBV.
    struct ShaderBindingsCompiledData
    {
        GpuDescriptorHandle m_gpuViewsDescriptorHandle    = {};
        GpuDescriptorHandle m_gpuSamplersDescriptorHandle = {};
        eastl::vector<GpuVirtualAddress> m_gpuConstantAddresses;
        eastl::vector<CpuVirtualAddress> m_cpuConstantAddresses;
    };

    /// DX12-side per-HLSL-space binding object. Owns the ring-buffered descriptor
    /// tables and constant memory once ShaderInputCompiler allocates them on the
    /// first Compile; before that, all GPU resources are empty (no descriptor heap
    /// pressure for unused ShaderBindings).
    class ShaderBindings final : public RHI::ShaderBindings
    {
    public:
        ShaderBindings() = default;
        ~ShaderBindings() override = default;

        const ShaderBindingsCompiledData& GetCompiledData() const
        {
            return m_compiledData[m_compiledDataIndex];
        }

    private:
        ResultCode InitInternal(RHI::Device& device, const Descriptor& descriptor) override;
        void       ShutdownInternal() override;

        friend class ShaderInputCompiler;
        friend class CommandList;

        // Lazy-allocated by ShaderInputCompiler on the first Compile.
        DescriptorTable m_viewsDescriptorTable;
        DescriptorTable m_samplersDescriptorTable;
        MemoryView      m_constantMemoryView;

        eastl::array<ShaderBindingsCompiledData, RHI::Limits::Device::FrameCountMax> m_compiledData;
        uint32_t m_compiledDataIndex = 0;

        /// SamplerState → Sampler instance cache, populated lazily during Compile.
        /// Mirrors the pattern in DX12::ShaderResource.
        eastl::unordered_map<RHI::SamplerState, Ptr<Sampler>, RHI::SamplerStateHasher> m_samplers;

        /// Hash of the PipelineLayoutDescriptor captured at first Compile; subsequent
        /// Compiles assert against this to catch the "wrong layout" misuse.
        size_t m_layoutHash = 0;
    };
}
