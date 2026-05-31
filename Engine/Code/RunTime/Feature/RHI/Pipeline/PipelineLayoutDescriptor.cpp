#include "PipelineLayoutDescriptor.h"

#include <EASTLEX/hash.h>
#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>

#include <RHI/Pipeline/ConstantsLayout.h>
#include <RHI/ValidationLayer.h>

namespace Spark::RHI
{
    bool PipelineLayoutDescriptor::IsFinalized() const
    {
        return m_hash != InvalidHash;
    }

    void PipelineLayoutDescriptor::Reset()
    {
        m_hash = InvalidHash;

        m_rootConstantsLayout = nullptr;

        m_bufferDescs.clear();
        m_imageDescs.clear();
        m_samplerDescs.clear();
        m_constantDescs.clear();
        m_staticSamplerDescs.clear();
        m_spaceGroups.clear();

        ResetInternal();
    }

    ResultCode PipelineLayoutDescriptor::Finalize()
    {
        ResultCode result = FinalizeInternal();
        if (result != ResultCode::Success)
        {
            return result;
        }

        BuildConstantBufferLayouts();

        size_t seed = 0;

        // hash 各类型 ShaderInput descriptor 数组
        for (const auto& d : m_bufferDescs)       { eastl::hash_combine_raw(seed, d.GetHash()); }
        for (const auto& d : m_imageDescs)         { eastl::hash_combine_raw(seed, d.GetHash()); }
        for (const auto& d : m_samplerDescs)       { eastl::hash_combine_raw(seed, d.GetHash()); }
        for (const auto& d : m_constantDescs)      { eastl::hash_combine_raw(seed, d.GetHash()); }
        for (const auto& e : m_staticSamplerDescs) { eastl::hash_combine_raw(seed, e.m_desc.GetHash()); }

        // root constants（push constant）
        if (m_rootConstantsLayout)
        {
            eastl::hash_combine_raw(seed, m_rootConstantsLayout->GetHash());
        }

        m_hash = GetHashInternal(seed);
        return ResultCode::Success;
    }

    size_t PipelineLayoutDescriptor::GetHash() const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_hash;
    }

    void PipelineLayoutDescriptor::ResetInternal() {}

    ResultCode PipelineLayoutDescriptor::FinalizeInternal()
    {
        return ResultCode::Success;
    }

    size_t PipelineLayoutDescriptor::GetHashInternal(size_t seed) const
    {
        return seed;
    }

    void PipelineLayoutDescriptor::ValidateShaderInputOverlapInternal(
        const ShaderInputHandle&, const ShaderInputHandle&, uint32_t) const
    {
    }

    //=========================================================================
    // Root constants（push constant）
    //=========================================================================

    void PipelineLayoutDescriptor::SetRootConstantsLayout(const ConstantsLayout& rootConstantsLayout)
    {
        m_rootConstantsLayout = const_cast<ConstantsLayout*>(&rootConstantsLayout);
    }

    const ConstantsLayout* PipelineLayoutDescriptor::GetRootConstantsLayout() const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_rootConstantsLayout.get();
    }

    //=========================================================================
    // ShaderInput API
    //=========================================================================

    void PipelineLayoutDescriptor::AddShaderInputDescriptors(
        const ShaderInputList& list, ShaderStageMask stageMask)
    {
        for (const ShaderInputBufferDescriptor& desc : list.m_buffers)
        {
            uint32_t index = static_cast<uint32_t>(m_bufferDescs.size());
            m_bufferDescs.push_back(desc);
            InsertShaderInput({ ShaderInputType::Buffer, index }, desc.m_spaceId, stageMask);
        }
        for (const ShaderInputImageDescriptor& desc : list.m_images)
        {
            uint32_t index = static_cast<uint32_t>(m_imageDescs.size());
            m_imageDescs.push_back(desc);
            InsertShaderInput({ ShaderInputType::Image, index }, desc.m_spaceId, stageMask);
        }
        for (const ShaderInputSamplerDescriptor& desc : list.m_samplers)
        {
            uint32_t index = static_cast<uint32_t>(m_samplerDescs.size());
            m_samplerDescs.push_back(desc);
            InsertShaderInput({ ShaderInputType::Sampler, index }, desc.m_spaceId, stageMask);
        }
        for (const ShaderInputConstantDescriptor& desc : list.m_constants)
        {
            uint32_t index = static_cast<uint32_t>(m_constantDescs.size());
            m_constantDescs.push_back(desc);
            InsertShaderInput({ ShaderInputType::Constant, index }, desc.m_spaceId, stageMask);
        }
    }

    void PipelineLayoutDescriptor::AddStaticSamplerDescriptor(
        const ShaderInputStaticSamplerDescriptor& desc, ShaderStageMask stageMask)
    {
        m_staticSamplerDescs.push_back({ desc, stageMask });
    }

    void PipelineLayoutDescriptor::InsertShaderInput(
        ShaderInputHandle newHandle, uint32_t spaceId, ShaderStageMask stageMask)
    {
        // 找到已有的同 spaceId group，或新建
        ShaderInputGroup* targetGroup = nullptr;
        for (ShaderInputGroup& group : m_spaceGroups)
        {
            if (group.m_spaceId == spaceId)
            {
                targetGroup = &group;
                break;
            }
        }

        if (targetGroup == nullptr)
        {
            ASSERT(m_spaceGroups.size() < Limits::Pipeline::ShaderInputGroupCountMax,
                "[PipelineLayoutDescriptor] Exceeded ShaderInputGroupCountMax (%u).",
                Limits::Pipeline::ShaderInputGroupCountMax);
            m_spaceGroups.push_back({ spaceId, stageMask, {} });
            targetGroup = &m_spaceGroups.back();
        }
        else
        {
            targetGroup->m_stageMask = targetGroup->m_stageMask | stageMask;
        }

        if (Validation::isEnabled)
        {
            for (const ShaderInputHandle& existing : targetGroup->m_shaderInputs)
            {
                ValidateShaderInputOverlapInternal(newHandle, existing, spaceId);
            }
        }

        targetGroup->m_shaderInputs.push_back(newHandle);
    }

    void PipelineLayoutDescriptor::BuildConstantBufferLayouts()
    {
        for (ShaderInputGroup& group : m_spaceGroups)
        {
            group.m_constantBuffers.clear();

            // Pass 1: dedup by registerId, expand byteSize = max(offset + count).
            for (const ShaderInputHandle& handle : group.m_shaderInputs)
            {
                if (handle.m_type != ShaderInputType::Constant)
                {
                    continue;
                }
                const ShaderInputConstantDescriptor& cd = m_constantDescs[handle.m_index];

                ConstantBufferLayout* slot = nullptr;
                for (auto& s : group.m_constantBuffers)
                {
                    if (s.m_registerId == cd.m_registerId)
                    {
                        slot = &s;
                        break;
                    }
                }
                if (slot == nullptr)
                {
                    ASSERT(group.m_constantBuffers.size() < ShaderInputGroup::ConstantBufferCountMax,
                        "[PipelineLayoutDescriptor] Exceeded ConstantBufferCountMax (%u) in space %u.",
                        ShaderInputGroup::ConstantBufferCountMax, group.m_spaceId);
                    group.m_constantBuffers.push_back({ cd.m_registerId, 0, 0 });
                    slot = &group.m_constantBuffers.back();
                }

                const uint32_t end = cd.m_constantByteOffset + cd.m_constantByteCount;
                slot->m_byteSize = eastl::max(slot->m_byteSize, end);
            }

            // Pass 2: align each slot to 256B and compute prefix-sum byteOffset.
            uint32_t offset = 0;
            for (auto& slot : group.m_constantBuffers)
            {
                slot.m_byteSize   = AlignUp(slot.m_byteSize, RHI::Alignment::Constant);
                slot.m_byteOffset = offset;
                offset += slot.m_byteSize;
            }
        }
    }

    size_t PipelineLayoutDescriptor::GetSpaceGroupCount() const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_spaceGroups.size();
    }

    const ShaderInputGroup& PipelineLayoutDescriptor::GetSpaceGroup(size_t index) const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_spaceGroups[index];
    }

    const ShaderInputGroup* PipelineLayoutDescriptor::FindSpaceGroupBySpaceId(uint32_t spaceId) const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        for (const ShaderInputGroup& group : m_spaceGroups)
        {
            if (group.m_spaceId == spaceId)
            {
                return &group;
            }
        }
        return nullptr;
    }

    const ShaderInputBufferDescriptor& PipelineLayoutDescriptor::GetBufferDescriptor(uint32_t index) const
    {
        return m_bufferDescs[index];
    }

    const ShaderInputImageDescriptor& PipelineLayoutDescriptor::GetImageDescriptor(uint32_t index) const
    {
        return m_imageDescs[index];
    }

    const ShaderInputSamplerDescriptor& PipelineLayoutDescriptor::GetSamplerDescriptor(uint32_t index) const
    {
        return m_samplerDescs[index];
    }

    const ShaderInputConstantDescriptor& PipelineLayoutDescriptor::GetConstantDescriptor(uint32_t index) const
    {
        return m_constantDescs[index];
    }

    size_t PipelineLayoutDescriptor::GetStaticSamplerCount() const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_staticSamplerDescs.size();
    }

    const ShaderInputStaticSamplerDescriptor& PipelineLayoutDescriptor::GetStaticSamplerDescriptor(uint32_t index) const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_staticSamplerDescs[index].m_desc;
    }

    ShaderStageMask PipelineLayoutDescriptor::GetStaticSamplerStageMask(uint32_t index) const
    {
        ASSERT(IsFinalized(), "[PipelineLayoutDescriptor] Accessor called on a non-finalized descriptor.");
        return m_staticSamplerDescs[index].m_stageMask;
    }

    const ShaderInputBufferDescriptor* PipelineLayoutDescriptor::FindBufferDescriptor(const InputName& name) const
    {
        for (const ShaderInputBufferDescriptor& d : m_bufferDescs)
        {
            if (d.m_name == name) { return &d; }
        }
        return nullptr;
    }

    const ShaderInputImageDescriptor* PipelineLayoutDescriptor::FindImageDescriptor(const InputName& name) const
    {
        for (const ShaderInputImageDescriptor& d : m_imageDescs)
        {
            if (d.m_name == name) { return &d; }
        }
        return nullptr;
    }

    const ShaderInputSamplerDescriptor* PipelineLayoutDescriptor::FindSamplerDescriptor(const InputName& name) const
    {
        for (const ShaderInputSamplerDescriptor& d : m_samplerDescs)
        {
            if (d.m_name == name) { return &d; }
        }
        return nullptr;
    }

    const ShaderInputConstantDescriptor* PipelineLayoutDescriptor::FindConstantDescriptor(const InputName& name) const
    {
        for (const ShaderInputConstantDescriptor& d : m_constantDescs)
        {
            if (d.m_name == name) { return &d; }
        }
        return nullptr;
    }

} // namespace Spark::RHI
