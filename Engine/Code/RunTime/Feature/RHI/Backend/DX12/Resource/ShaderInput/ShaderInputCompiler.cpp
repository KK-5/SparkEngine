#include "ShaderInputCompiler.h"

#include <Log/ILogSystem.h>

#include <ID3D12Factory.h>
#include <Conversions.h>
#include <Device/Device.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/Buffer/BufferView.h>
#include <Resource/Image/ImageView.h>
#include <Resource/Sampler/Sampler.h>
#include <Resource/Constant/ConstantBufferContext.h>

namespace Spark::RHI::DX12
{
    ResultCode ShaderInputCompiler::InitInternal(
        RHI::Device& /*device*/, const RHI::ShaderInputCompilerDescriptor& /*desc*/)
    {
        return ResultCode::Success;
    }

    ResultCode ShaderInputCompiler::CompileInternal(RHI::ShaderBindings& base)
    {
        ShaderBindings& b = static_cast<ShaderBindings&>(base);

        const bool firstCompile =
            !b.m_viewsDescriptorTable.IsValid()
            && !b.m_samplersDescriptorTable.IsValid()
            && !b.m_constantMemoryView.IsValid();

        if (firstCompile)
        {
            AllocateBindings(b);
            b.m_layoutHash = b.GetLayoutDescriptor().GetHash();
        }
        else
        {
            ASSERT(b.m_layoutHash == b.GetLayoutDescriptor().GetHash(),
                "[ShaderInputCompiler] ShaderBindings layout hash changed since first compile.");
        }

        UpdateBindings(b);
        return ResultCode::Success;
    }

    //////////////////////////////////////////////////////////////////////////
    // Allocate

    void ShaderInputCompiler::AllocateBindings(ShaderBindings& b)
    {
        Device& device = static_cast<Device&>(GetDevice());
        ID3D12FactoryInterface* factory = Service<ID3D12FactoryInterface>::Get();
        DescriptorContext& descriptorCtx = factory->AcquireDescriptorContext(device);

        const uint32_t frameCountMax = device.GetDescriptor().m_frameCountMax;

        const RHI::PipelineLayoutDescriptor& desc = b.GetLayoutDescriptor();
        const RHI::ShaderInputGroup* group = desc.FindSpaceGroupBySpaceId(b.GetSpaceId());
        ASSERT(group, "[ShaderInputCompiler] SpaceId not found in PipelineLayoutDescriptor.");

        const uint32_t viewsPerFrame         = ComputeViewsPerFrame(desc, *group);
        const uint32_t samplersPerFrame      = ComputeSamplersPerFrame(desc, *group);
        const uint32_t constantBytesPerFrame = group->GetConstantBytesPerFrame();

        // -- Views descriptor table (ring-buffered) --
        if (viewsPerFrame > 0)
        {
            const uint32_t ringSize = viewsPerFrame * frameCountMax;
            b.m_viewsDescriptorTable =
                descriptorCtx.CreateDescriptorTable(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ringSize);
            ASSERT(b.m_viewsDescriptorTable.IsValid(),
                "[ShaderInputCompiler] Failed to allocate views descriptor table.");

            for (uint32_t i = 0; i < frameCountMax; ++i)
            {
                DescriptorTable frame(b.m_viewsDescriptorTable[viewsPerFrame * i], viewsPerFrame);
                b.m_compiledData[i].m_gpuViewsDescriptorHandle =
                    descriptorCtx.GetGpuNativeHandleForTable(frame);
            }
        }

        // -- Samplers descriptor table (ring-buffered) --
        if (samplersPerFrame > 0)
        {
            const uint32_t ringSize = samplersPerFrame * frameCountMax;
            b.m_samplersDescriptorTable =
                descriptorCtx.CreateDescriptorTable(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, ringSize);
            ASSERT(b.m_samplersDescriptorTable.IsValid(),
                "[ShaderInputCompiler] Failed to allocate samplers descriptor table.");

            for (uint32_t i = 0; i < frameCountMax; ++i)
            {
                DescriptorTable frame(b.m_samplersDescriptorTable[samplersPerFrame * i], samplersPerFrame);
                b.m_compiledData[i].m_gpuSamplersDescriptorHandle =
                    descriptorCtx.GetGpuNativeHandleForTable(frame);
            }
        }

        // -- Constant buffer (ring-buffered, packed per-register per group->m_constantBuffers) --
        if (constantBytesPerFrame > 0)
        {
            ConstantBufferContext& constantBufferCtx = factory->AcquireConstantBufferContext(device);
            const uint32_t ringSize = constantBytesPerFrame * frameCountMax;
            b.m_constantMemoryView =
                constantBufferCtx.CreateConstantBuffer(ringSize, RHI::Alignment::Constant);
            ASSERT(b.m_constantMemoryView.IsValid(),
                "[ShaderInputCompiler] Failed to allocate constant memory.");

            CpuVirtualAddress cpuBase = b.m_constantMemoryView.Map(RHI::HostMemoryAccess::Write);
            GpuVirtualAddress gpuBase = b.m_constantMemoryView.GetGpuAddress();

            for (uint32_t i = 0; i < frameCountMax; ++i)
            {
                auto& frameData = b.m_compiledData[i];
                frameData.m_gpuConstantAddresses.reserve(group->m_constantBuffers.size());
                frameData.m_cpuConstantAddresses.reserve(group->m_constantBuffers.size());
                for (const auto& slot : group->m_constantBuffers)
                {
                    const uint32_t base = constantBytesPerFrame * i + slot.m_byteOffset;
                    frameData.m_gpuConstantAddresses.push_back(gpuBase + base);
                    frameData.m_cpuConstantAddresses.push_back(cpuBase + base);
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Update

    void ShaderInputCompiler::UpdateBindings(ShaderBindings& b)
    {
        const uint32_t frameCountMax = static_cast<Device&>(GetDevice()).GetDescriptor().m_frameCountMax;
        b.m_compiledDataIndex = (b.m_compiledDataIndex + 1) % frameCountMax;
        const uint32_t frameIdx = b.m_compiledDataIndex;

        const RHI::PipelineLayoutDescriptor& desc  = b.GetLayoutDescriptor();
        const RHI::ShaderInputGroup* group         = desc.FindSpaceGroupBySpaceId(b.GetSpaceId());
        const uint32_t viewsPerFrame    = ComputeViewsPerFrame(desc, *group);
        const uint32_t samplersPerFrame = ComputeSamplersPerFrame(desc, *group);

        if (viewsPerFrame > 0 && b.m_viewsDescriptorTable.IsValid())
        {
            DescriptorTable frame(b.m_viewsDescriptorTable[frameIdx * viewsPerFrame], viewsPerFrame);
            UpdateViewsDescriptorTable(frame, b);
        }

        if (samplersPerFrame > 0 && b.m_samplersDescriptorTable.IsValid())
        {
            DescriptorTable frame(b.m_samplersDescriptorTable[frameIdx * samplersPerFrame], samplersPerFrame);
            UpdateSamplersDescriptorTable(frame, b);
        }

        if (b.m_constantMemoryView.IsValid())
        {
            UpdateConstants(b);
        }
    }

    void ShaderInputCompiler::UpdateViewsDescriptorTable(
        DescriptorTable frameTable,
        ShaderBindings& b)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        const RHI::PipelineLayoutDescriptor& desc = b.GetLayoutDescriptor();
        const RHI::ShaderInputGroup* group = desc.FindSpaceGroupBySpaceId(b.GetSpaceId());

        // Single per-table source array — one CopyDescriptors call covers the whole sub-table.
        eastl::fixed_vector<DescriptorHandle, ViewsFixedSize> sources;
        sources.reserve(frameTable.GetSize());

        for (const RHI::ShaderInputHandle& handle : group->m_shaderInputs)
        {
            if (handle.m_type == RHI::ShaderInputType::Buffer)
            {
                const auto& bufferDesc = desc.GetBufferDescriptor(handle.m_index);
                const RHI::ShaderInputBuffer* input = b.FindBufferInput(bufferDesc.m_name);
                ASSERT(input, "[ShaderInputCompiler] Buffer ShaderInput missing: %s",
                    bufferDesc.m_name.GetCStr());

                switch (ConvertShaderInputBufferAccess(bufferDesc.m_access))
                {
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                    AppendCBVsFromBufferViews(input->GetViews(), sources);
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                    AppendSRVsFromViews<RHI::BufferView, BufferView>(
                        input->GetViews(), D3D12_SRV_DIMENSION_BUFFER, sources);
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                    AppendUAVsFromViews<RHI::BufferView, BufferView>(
                        input->GetViews(), D3D12_UAV_DIMENSION_BUFFER, sources);
                    break;
                default:
                    ASSERT(false, "[ShaderInputCompiler] Unhandled buffer descriptor range type.");
                    break;
                }
            }
            else if (handle.m_type == RHI::ShaderInputType::Image)
            {
                const auto& imageDesc = desc.GetImageDescriptor(handle.m_index);
                const RHI::ShaderInputImage* input = b.FindImageInput(imageDesc.m_name);
                ASSERT(input, "[ShaderInputCompiler] Image ShaderInput missing: %s",
                    imageDesc.m_name.GetCStr());

                switch (ConvertShaderInputImageAccess(imageDesc.m_access))
                {
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                    AppendSRVsFromViews<RHI::ImageView, ImageView>(
                        input->GetViews(), ConvertSRVDimension(imageDesc.m_type), sources);
                    break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                    AppendUAVsFromViews<RHI::ImageView, ImageView>(
                        input->GetViews(), ConvertUAVDimension(imageDesc.m_type), sources);
                    break;
                default:
                    ASSERT(false, "[ShaderInputCompiler] Unhandled image descriptor range type.");
                    break;
                }
            }
        }

        ASSERT(sources.size() == frameTable.GetSize(),
            "[ShaderInputCompiler] Views source count (%u) != table size (%u).",
            static_cast<uint32_t>(sources.size()), frameTable.GetSize());
        descriptorCtx.UpdateDescriptorTableRange(
            frameTable, sources.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void ShaderInputCompiler::UpdateSamplersDescriptorTable(
        DescriptorTable frameTable,
        ShaderBindings& b)
    {
        Device& device = static_cast<Device&>(GetDevice());
        ID3D12FactoryInterface* factory = Service<ID3D12FactoryInterface>::Get();
        DescriptorContext& descriptorCtx = factory->AcquireDescriptorContext(device);

        const RHI::PipelineLayoutDescriptor& desc = b.GetLayoutDescriptor();
        const RHI::ShaderInputGroup* group = desc.FindSpaceGroupBySpaceId(b.GetSpaceId());

        eastl::fixed_vector<DescriptorHandle, ViewsFixedSize> sources;
        sources.reserve(frameTable.GetSize());

        for (const RHI::ShaderInputHandle& handle : group->m_shaderInputs)
        {
            if (handle.m_type != RHI::ShaderInputType::Sampler)
            {
                continue;
            }
            const auto& samplerDesc = desc.GetSamplerDescriptor(handle.m_index);
            const RHI::ShaderInputSampler* input = b.FindSamplerInput(samplerDesc.m_name);
            ASSERT(input, "[ShaderInputCompiler] Sampler ShaderInput missing: %s",
                samplerDesc.m_name.GetCStr());

            for (const RHI::SamplerState& state : input->GetStates())
            {
                auto it = b.m_samplers.find(state);
                if (it == b.m_samplers.end())
                {
                    Ptr<Sampler> sampler = factory->CreateSampler();
                    sampler->Init(device, state);
                    auto [inserted, ok] = b.m_samplers.insert({ state, sampler });
                    sources.push_back(inserted->second->GetDescriptorHandle());
                }
                else
                {
                    sources.push_back(it->second->GetDescriptorHandle());
                }
            }
        }

        ASSERT(sources.size() == frameTable.GetSize(),
            "[ShaderInputCompiler] Sampler source count (%u) != table size (%u).",
            static_cast<uint32_t>(sources.size()), frameTable.GetSize());
        descriptorCtx.UpdateDescriptorTableRange(
            frameTable, sources.data(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    void ShaderInputCompiler::UpdateConstants(ShaderBindings& b)
    {
        const RHI::PipelineLayoutDescriptor& desc = b.GetLayoutDescriptor();
        const RHI::ShaderInputGroup* group = desc.FindSpaceGroupBySpaceId(b.GetSpaceId());

        if (group->m_constantBuffers.empty())
        {
            return;
        }

        const uint32_t frameIdx = b.m_compiledDataIndex;
        const auto& cpuAddresses = b.m_compiledData[frameIdx].m_cpuConstantAddresses;

        for (const RHI::ShaderInputHandle& handle : group->m_shaderInputs)
        {
            if (handle.m_type != RHI::ShaderInputType::Constant)
            {
                continue;
            }
            const auto& cd = desc.GetConstantDescriptor(handle.m_index);

            uint32_t k = 0;
            for (; k < group->m_constantBuffers.size(); ++k)
            {
                if (group->m_constantBuffers[k].m_registerId == cd.m_registerId) break;
            }
            ASSERT(k < group->m_constantBuffers.size(),
                "[ShaderInputCompiler] Constant register %u not present in m_constantBuffers.",
                cd.m_registerId);

            const RHI::ShaderInputConstant* input = b.FindConstantInput(cd.m_name);
            ASSERT(input, "[ShaderInputCompiler] Constant ShaderInput missing: %s",
                cd.m_name.GetCStr());

            eastl::span<const uint8_t> data = input->GetData();
            ASSERT(data.size() == cd.m_constantByteCount,
                "[ShaderInputCompiler] Constant data size mismatch for %s.",
                cd.m_name.GetCStr());
            memcpy(cpuAddresses[k] + cd.m_constantByteOffset, data.data(), data.size());
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Descriptor gather helpers

    template <typename TBase, typename TDx12>
    void ShaderInputCompiler::AppendSRVsFromViews(
        eastl::span<const ConstPtr<TBase>> views,
        D3D12_SRV_DIMENSION dimension,
        eastl::fixed_vector<DescriptorHandle, ViewsFixedSize>& out)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        const DescriptorHandle nullHandle = descriptorCtx.GetNullHandleSRV(dimension);

        for (const auto& view : views)
        {
            out.push_back(view
                ? static_cast<const TDx12*>(view.get())->GetReadDescriptor()
                : nullHandle);
        }
    }

    template <typename TBase, typename TDx12>
    void ShaderInputCompiler::AppendUAVsFromViews(
        eastl::span<const ConstPtr<TBase>> views,
        D3D12_UAV_DIMENSION dimension,
        eastl::fixed_vector<DescriptorHandle, ViewsFixedSize>& out)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        const DescriptorHandle nullHandle = descriptorCtx.GetNullHandleUAV(dimension);

        for (const auto& view : views)
        {
            out.push_back(view
                ? static_cast<const TDx12*>(view.get())->GetReadWriteDescriptor()
                : nullHandle);
        }
    }

    void ShaderInputCompiler::AppendCBVsFromBufferViews(
        eastl::span<const ConstPtr<RHI::BufferView>> views,
        eastl::fixed_vector<DescriptorHandle, ViewsFixedSize>& out)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        const DescriptorHandle nullHandle = descriptorCtx.GetNullHandleCBV();

        for (const auto& view : views)
        {
            out.push_back(view
                ? static_cast<const BufferView*>(view.get())->GetConstantDescriptor()
                : nullHandle);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sub-range size helpers

    uint32_t ShaderInputCompiler::ComputeViewsPerFrame(
        const RHI::PipelineLayoutDescriptor& desc,
        const RHI::ShaderInputGroup& group)
    {
        uint32_t total = 0;
        for (const RHI::ShaderInputHandle& handle : group.m_shaderInputs)
        {
            if (handle.m_type == RHI::ShaderInputType::Buffer)
            {
                total += desc.GetBufferDescriptor(handle.m_index).m_count;
            }
            else if (handle.m_type == RHI::ShaderInputType::Image)
            {
                total += desc.GetImageDescriptor(handle.m_index).m_count;
            }
        }
        return total;
    }

    uint32_t ShaderInputCompiler::ComputeSamplersPerFrame(
        const RHI::PipelineLayoutDescriptor& desc,
        const RHI::ShaderInputGroup& group)
    {
        uint32_t total = 0;
        for (const RHI::ShaderInputHandle& handle : group.m_shaderInputs)
        {
            if (handle.m_type == RHI::ShaderInputType::Sampler)
            {
                total += desc.GetSamplerDescriptor(handle.m_index).m_count;
            }
        }
        return total;
    }
}
