#include "ShaderBindings.h"

#include <Log/ILogSystem.h>

#include <ID3D12Factory.h>
#include <Device/Device.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/Constant/ConstantBufferContext.h>

namespace Spark::RHI::DX12
{
    ResultCode ShaderBindings::InitInternal(RHI::Device& /*device*/, const Descriptor& /*descriptor*/)
    {
        // All GPU resources (descriptor tables, constant memory) are allocated lazily
        // by ShaderInputCompiler on the first Compile. Nothing to do here -- the RHI
        // base class has already built the ShaderInput data containers from the layout.
        return ResultCode::Success;
    }

    void ShaderBindings::ShutdownInternal()
    {
        Device& device = static_cast<Device&>(GetDevice());
        ID3D12FactoryInterface* factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(factory, "[ShaderBindings] ID3D12Factory is null.");

        if (m_viewsDescriptorTable.IsValid() || m_samplersDescriptorTable.IsValid())
        {
            DescriptorContext& descriptorCtx = factory->AcquireDescriptorContext(device);
            if (m_viewsDescriptorTable.IsValid())
            {
                descriptorCtx.ReleaseDescriptorTable(m_viewsDescriptorTable);
            }
            if (m_samplersDescriptorTable.IsValid())
            {
                descriptorCtx.ReleaseDescriptorTable(m_samplersDescriptorTable);
            }
        }

        if (m_constantMemoryView.IsValid())
        {
            ConstantBufferContext& constantBufferCtx = factory->AcquireConstantBufferContext(device);
            constantBufferCtx.CollectConstantBuffer(m_constantMemoryView);
        }

        // Releasing frees the pool allocation but leaves the handle value, so these still
        // report IsValid(). This object is pooled and recycled: a reused instance must look
        // un-compiled (all three invalid) or the next Compile takes the "already compiled"
        // path with a stale (zeroed) m_layoutHash and asserts.
        m_viewsDescriptorTable    = {};
        m_samplersDescriptorTable = {};
        m_constantMemoryView      = {};

        for (auto& frame : m_compiledData)
        {
            frame = {};
        }
        m_compiledDataIndex = 0;
        m_samplers.clear();
        m_layoutHash        = 0;
    }
}
