/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineLayoutDescriptor.h"

#include <EASTLEX/hash.h>
#include <Log/SpdLogSystem.h>

#include <RHI/Resource/ShaderResource/ConstantsLayout.h>
#include <RHi/Resource/ShaderResource/ShaderResourceLayout.h>

namespace Spark::RHI
{
    size_t ResourceBindingInfo::GetHash() const
    {
        size_t hash = eastl::hash<uint32_t>()(static_cast<uint32_t>(m_shaderStageMask));
        eastl::hash_combine(hash, static_cast<uint32_t>(m_registerId));
        return hash;
    }

    size_t ShaderResourceBindingInfo::GetHash() const
    {
        size_t hash = m_constantDataBindingInfo.GetHash();
        for (const auto& resourceBindInfo: m_resourcesRegisterMap)
        {
            eastl::hash_combine_raw(hash, resourceBindInfo.second.GetHash());
        }
        return hash;
    }

    bool PipelineLayoutDescriptor::IsFinalized() const
    {
        return m_hash != InvalidHash;
    }

    void PipelineLayoutDescriptor::Reset()
    {
        m_hash = InvalidHash;
        m_shaderResourceLayoutsInfo.clear();
        m_bindingSlotToIndex.fill(RHI::Limits::Pipeline::ShaderResourceCountMax);
        ResetInternal();
    }

    ResultCode PipelineLayoutDescriptor::Finalize()
    {
        ResultCode resultCode = FinalizeInternal();

        if (resultCode == ResultCode::Success)
        {
            size_t seed { 0 };
            for (const ShaderResourceLayoutInfo& layoutInfo : m_shaderResourceLayoutsInfo)
            {
                eastl::hash_combine_raw(seed, layoutInfo.first->GetHash());
                eastl::hash_combine_raw(seed, layoutInfo.second.GetHash());
            }

            if (m_rootConstantsLayout)
            {
                eastl::hash_combine_raw(seed, m_rootConstantsLayout->GetHash());
            }

            for (const auto& index : m_bindingSlotToIndex)
            {
                eastl::hash_combine(seed, index);
            }

            m_hash = GetHashInternal(seed);
        }

        return resultCode;
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

    void PipelineLayoutDescriptor::AddShaderResourceLayoutInfo(const ShaderResourceLayout& layout, const ShaderResourceBindingInfo& shaderResourceBindingInfo)
    {           
        m_bindingSlotToIndex[layout.GetBindingSlot()] = static_cast<uint32_t>(m_shaderResourceLayoutsInfo.size());
        // NOTE: The const_cast is required because serialization does not allow for ConstPtr. However,
        // the layout is always treated as immutable internally, and is only exposed as such externally.
        m_shaderResourceLayoutsInfo.push_back({ const_cast<ShaderResourceLayout*>(&layout), shaderResourceBindingInfo });
    }

    void PipelineLayoutDescriptor::SetRootConstantsLayout(const ConstantsLayout& rootConstantsLayout)
    {
        // NOTE: The const_cast is required because serialization does not allow for ConstPtr.However,
        // the layout is always treated as immutable internally, and is only exposed as such externally.
        m_rootConstantsLayout = const_cast<ConstantsLayout*>(&rootConstantsLayout);
    }

    size_t PipelineLayoutDescriptor::GetShaderResourceLayoutCount() const
    {
        ASSERT(IsFinalized(), "Accessor called on a non-finalized pipeline layout. This is not permitted.");

        return m_shaderResourceLayoutsInfo.size();
    }

    const ShaderResourceLayout* PipelineLayoutDescriptor::GetShaderResourceLayout(size_t index) const
    {
        ASSERT(IsFinalized(), "Accessor called on a non-finalized pipeline layout. This is not permitted.");
        return m_shaderResourceLayoutsInfo[index].first.get();
    }

    const ShaderResourceBindingInfo& PipelineLayoutDescriptor::GetShaderResourceBindingInfo(size_t index) const
    {
        ASSERT(IsFinalized(), "Accessor called on a non-finalized pipeline layout. This is not permitted.");
        return m_shaderResourceLayoutsInfo[index].second;
    }

    const ConstantsLayout* PipelineLayoutDescriptor::GetRootConstantsLayout() const
    {
        ASSERT(IsFinalized(), "Accessor called on a non-finalized pipeline layout. This is not permitted.");
        return m_rootConstantsLayout.get();
    }

    size_t PipelineLayoutDescriptor::GetHash() const
    {
        ASSERT(IsFinalized(), "Accessor called on a non-finalized pipeline layout. This is not permitted.");
        return m_hash;
    }

    uint32_t PipelineLayoutDescriptor::GetShaderResourceIndexFromBindingSlot(uint32_t bindingSlot) const
    {
        ASSERT(IsFinalized(), "Accessor called on a non-finalized pipeline layout. This is not permitted.");
        return m_bindingSlotToIndex[bindingSlot];
    }
}