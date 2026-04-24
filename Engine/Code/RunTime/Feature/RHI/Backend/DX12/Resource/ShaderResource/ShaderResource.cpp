/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Device/Device.h>
#include <Resource/Constant/ConstantBufferContext.h>
#include <Descriptor/DescriptorContext.h>
#include <ID3D12Factory.h>

#include "ShaderResource.h"

namespace Spark::RHI::DX12
{
    const ShaderResourceCompiledData& ShaderResource::GetCompiledData() const
    {
        return m_compiledData[m_compiledDataIndex];
    }

    void ShaderResource::ShutdownInternal()
    {
        Device& device = static_cast<Device&>(GetDevice());
        ID3D12FactoryInterface* factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(factory, "ID3D12Factory is null.");

        DescriptorContext& descriptorCtx = factory->AcquireDescriptorContext(device);
        ConstantBufferContext& constantBufferCtx = factory->AcquireConstantBufferContext(device);

        if (m_constantMemoryView.IsValid())
        {
            // 常量缓冲区创建时处于Map状态
            m_constantMemoryView.Unmap(RHI::HostMemoryAccess::Write);
            constantBufferCtx.CollectConstantBuffer(m_constantMemoryView);
        }

        if (m_viewsDescriptorTable.IsValid())
        {
            descriptorCtx.ReleaseDescriptorTable(m_viewsDescriptorTable);
        }

        if (m_samplersDescriptorTable.IsValid())
        {
            descriptorCtx.ReleaseDescriptorTable(m_samplersDescriptorTable);
        }

        m_samplers.clear();
    }
} 