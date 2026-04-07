/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/array.h>
#include <RHI/Resource/ShaderResource/ShaderResource.h>
#include <RHI/Device/DeviceObjectFactory.h>

#include <MemoryView.h>
#include <Descriptor/Descriptor.h>

namespace Spark::RHI::DX12
{
    class Sampler;
    
    struct ShaderResourceCompiledData
    {
        /// The GPU descriptor handle for views to bind to the command list.
        GpuDescriptorHandle m_gpuViewsDescriptorHandle = {};

        /// The GPU descriptor handle for samplers to bind to the command list.
        GpuDescriptorHandle m_gpuSamplersDescriptorHandle = {};

        /// The constant buffer GPU virtual address.
        GpuVirtualAddress m_gpuConstantAddress = {};

        /// The constant buffer CPU virtual address.
        CpuVirtualAddress m_cpuConstantAddress = {};
    };

    class ShaderResource final : public RHI::ShaderResource
    {
        using Base = RHI::ShaderResource;
    public:
        virtual ~ShaderResource() noexcept = default;

        const ShaderResourceCompiledData& GetCompiledData() const;

    private:
        ShaderResource() = default;

        void ShutdownInternal() override;

        friend class ShaderResourcePool;
        friend class ShaderResourceCompiler;
        friend class DescriptorContext;
        friend class DeviceObjectFactory<ShaderResource>;

        /// The current index into the compiled data array.
        uint32_t m_compiledDataIndex = 0;

        /// The array of compiled SRG data, N buffered for CPU updates.
        eastl::array<ShaderResourceCompiledData, RHI::Limits::Device::FrameCountMax> m_compiledData;

        /// The mapped memory view to constant memory.
        MemoryView m_constantMemoryView;

        /// The allocated descriptor table for vews.
        DescriptorTable m_viewsDescriptorTable;

        /// The allocated descriptor table for samplers.
        DescriptorTable m_samplersDescriptorTable; 

        /// 上层ShaderResource不持有具体Sampler，只有SamplerState，暂时由底层ShaderResource持有
        // eastl::vector<ConstPtr<Sampler>> m_samplers;
        eastl::unordered_map<RHI::SamplerState, Ptr<Sampler>, RHI::SamplerStateHasher> m_samplers;

        /// The descriptor tables for unbounded arrays.  Allocated on demand.
        // eastl::array<DescriptorTable, ShaderResourceGroupCompiledData::MaxUnboundedArrays * RHI::Limits::Device::FrameCountMax> m_unboundedDescriptorTables;
    };
}