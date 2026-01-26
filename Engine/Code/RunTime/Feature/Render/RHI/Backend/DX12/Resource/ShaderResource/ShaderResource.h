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

#include <MemoryView.h>

namespace Spark::RHI::DX12
{
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

        friend class ShaderResourceGroupPool;
        friend class DescriptorContext;

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

        /// The descriptor tables for unbounded arrays.  Allocated on demand.
        // eastl::array<DescriptorTable, ShaderResourceGroupCompiledData::MaxUnboundedArrays * RHI::Limits::Device::FrameCountMax> m_unboundedDescriptorTables;
    };
}