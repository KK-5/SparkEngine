/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderResourceLayout.h"

#include <Base.h>
#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    void ShaderResourceLayout::AddStaticSampler(const ShaderInputStaticSamplerDescriptor& sampler)
    {
        m_staticSamplers.push_back(sampler);
    }

    void ShaderResourceLayout::AddShaderInput(const ShaderInputBufferDescriptor& buffer)
    {
        m_inputsForBuffers.push_back(buffer);
    }

    void ShaderResourceLayout::AddShaderInput(const ShaderInputImageDescriptor& image)
    {
        m_inputsForImages.push_back(image);
    }

    void ShaderResourceLayout::AddShaderInput(const ShaderInputBufferUnboundedArrayDescriptor& bufferUnboundedArray)
    {
        m_inputsForBufferUnboundedArrays.push_back(bufferUnboundedArray);
    }

    void ShaderResourceLayout::AddShaderInput(const ShaderInputImageUnboundedArrayDescriptor& imageUnboundedArray)
    {
        m_inputsForImageUnboundedArrays.push_back(imageUnboundedArray);
    }

    void ShaderResourceLayout::AddShaderInput(const ShaderInputSamplerDescriptor& sampler)
    {
        m_inputsForSamplers.push_back(sampler);
    }

    void ShaderResourceLayout::AddShaderInput(const ShaderInputConstantDescriptor& constant)
    {
        m_constantsDataLayout->AddShaderInput(constant);
    }

    void ShaderResourceLayout::SetBindingSlot(uint32_t bindingSlot)
    {
        m_bindingSlot = bindingSlot;
    }

    eastl::span<const ShaderInputStaticSamplerDescriptor> ShaderResourceLayout::GetStaticSamplers() const
    {
        return m_staticSamplers;
    }

    ShaderInputIndex ShaderResourceLayout::FindShaderInputBufferIndex(const ShaderInputName& name) const
    {
        auto it = m_buffersNameMap.find(name);
        if (it != m_buffersNameMap.end())
        {
            return it->second;
        }
        return InvalidShaderInputIndex;
    }

    ShaderInputIndex ShaderResourceLayout::FindShaderInputImageIndex(const ShaderInputName& name) const
    {
        auto it = m_imagesNameMap.find(name);
        if (it != m_imagesNameMap.end())
        {
            return it->second;
        }
        return InvalidShaderInputIndex;
    }

    ShaderInputIndex ShaderResourceLayout::FindShaderInputSamplerIndex(const ShaderInputName& name) const
    {
        auto it = m_samplersNameMap.find(name);
        if (it != m_samplersNameMap.end())
        {
            return it->second;
        }
        return InvalidShaderInputIndex;
    }

    ShaderInputIndex ShaderResourceLayout::FindShaderInputConstantIndex(const ShaderInputName& name) const
    {
        return m_constantsDataLayout->FindShaderInputIndex(name);
    }

    ShaderInputIndex ShaderResourceLayout::FindShaderInputBufferUnboundedArrayIndex(const ShaderInputName& name) const
    {
        auto it = m_unboundedBufferArrayMap.find(name);
        if (it != m_unboundedBufferArrayMap.end())
        {
            return it->second;
        }
        return InvalidShaderInputIndex;
    }

    ShaderInputIndex ShaderResourceLayout::FindShaderInputImageUnboundedArrayIndex(const ShaderInputName& name) const
    {
        auto it = m_unboundedImageArrayMap.find(name);
        if (it != m_unboundedImageArrayMap.end())
        {
            return it->second;
        }
        return InvalidShaderInputIndex;
    }

    const ShaderInputBufferDescriptor& ShaderResourceLayout::GetShaderInputForBuffer(ShaderInputIndex index) const
    {
        return m_inputsForBuffers[index];
    }
        
    const ShaderInputImageDescriptor& ShaderResourceLayout::GetShaderInputForImage(ShaderInputIndex index) const
    {
        return m_inputsForImages[index];
    }

    const ShaderInputSamplerDescriptor& ShaderResourceLayout::GetShaderInputForSampler(ShaderInputIndex index) const
    {
        return m_inputsForSamplers[index];
    }

    const ShaderInputConstantDescriptor& ShaderResourceLayout::GetShaderInputForConstant(ShaderInputIndex index) const
    {
        return m_constantsDataLayout->GetShaderInput(index);
    }

    const ShaderInputBufferUnboundedArrayDescriptor& ShaderResourceLayout::GetShaderInputForBufferUnboundedArray(ShaderInputIndex index) const
    {
        return m_inputsForBufferUnboundedArrays[index];
    }

    const ShaderInputImageUnboundedArrayDescriptor& ShaderResourceLayout::GetShaderInputForImageUnboundArray(ShaderInputIndex index) const
    {
        return m_inputsForImageUnboundedArrays[index];
    }

    eastl::span<const ShaderInputBufferDescriptor> ShaderResourceLayout::GetShaderInputListForBuffers() const
    {
        return m_inputsForBuffers;
    }

    eastl::span<const ShaderInputImageDescriptor> ShaderResourceLayout::GetShaderInputListForImages() const
    {
        return m_inputsForImages;
    }

    eastl::span<const ShaderInputSamplerDescriptor> ShaderResourceLayout::GetShaderInputListForSamplers() const
    {
        return m_inputsForSamplers;
    }

    eastl::span<const ShaderInputConstantDescriptor> ShaderResourceLayout::GetShaderInputListForConstants() const
    {
        m_constantsDataLayout->GetShaderInputList();
    }

    eastl::span<const ShaderInputBufferUnboundedArrayDescriptor> ShaderResourceLayout::GetShaderInputListForBufferUnboundedArrays() const
    {
        return m_inputsForBufferUnboundedArrays;
    }

    eastl::span<const ShaderInputImageUnboundedArrayDescriptor> ShaderResourceLayout::GetShaderInputListForImageUnboundedArrays() const
    {
        return m_inputsForImageUnboundedArrays;
    }

    Interval ShaderResourceLayout::GetGroupInterval(ShaderInputIndex inputIndex) const
    {
        return m_intervalsForBuffers[inputIndex];
    }

    Interval ShaderResourceLayout::GetGroupInterval(ShaderInputIndex inputIndex) const
    {
        m_intervalsForImages[inputIndex];
    }

    Interval ShaderResourceLayout::GetGroupInterval(ShaderInputIndex inputIndex) const
    {
        m_intervalsForSamplers[inputIndex];
    }

    Interval ShaderResourceLayout::GetConstantInterval(ShaderInputIndex inputIndex) const
    {
        m_constantsDataLayout->GetInterval(inputIndex);
    }

    uint32_t ShaderResourceLayout::GetBuffersSize() const
    {
        return m_bufferSize;
    }

    uint32_t ShaderResourceLayout::GetImagesSize() const
    {
        return m_imageSize;
    }

    uint32_t ShaderResourceLayout::GetBufferUnboundedArraySize() const
    {
        return m_bufferUnboundedArraySize;
    }

    uint32_t ShaderResourceLayout::GetImageUnboundedArraySize() const
    {
        return m_imageUnboundedArraySize;
    }

    uint32_t ShaderResourceLayout::GetSamplersSize() const
    {
        return m_samplerSize;
    }

    uint32_t ShaderResourceLayout::GetConstantDataSize() const
    {
        return m_constantsDataLayout->GetDataSize();
    }

    const ConstantsLayout* ShaderResourceLayout::GetConstantsLayout() const
    {
        return m_constantsDataLayout.get();
    }

    uint32_t ShaderResourceLayout::GetBindingSlot() const
    {
        ASSERT(IsFinalized(), "ShaderResourceLayout is not finalized");
        return m_bindingSlot;
    }

    size_t ShaderResourceLayout::GetHash() const
    {
        ASSERT(IsFinalized(), "ShaderResourceLayout is not finalized");
        return m_hash;
    }

    bool ShaderResourceLayout::ValidateConstantsAccess(RHI::ShaderInputIndex inputIndex) const
    {
        return m_constantsDataLayout->ValidateAccess(inputIndex);
    }

    bool ShaderResourceLayout::ValidateAccess(ShaderInputIndex inputIndex, size_t inputIndexLimit, const char* inputArrayTypeName) const
    {
        if (Validation::isEnabled)
        {
            if (inputIndex >= inputIndexLimit)
            {
                ASSERT(false, "{} Input index '{}' out of range [0,{}).", inputArrayTypeName, inputIndex, inputIndexLimit);
                return false;
            }
        }
        return true;
    }

    bool ShaderResourceLayout::ValidateBufferIndexAccess(ShaderInputIndex inputIndex, uint32_t arrayIndex) const
    {
        if (Validation::isEnabled)
        {
            if (!ValidateAccess(inputIndex, m_inputsForBuffers.size(), "Buffer"))
            {
                return false;
            }

            const ShaderInputBufferDescriptor& descriptor = GetShaderInputForBuffer(inputIndex);
            if (arrayIndex >= descriptor.m_count)
            {
                ASSERT(false, "Buffer Input '{}[{}]': Array index '{}' out of range [0,{}).", descriptor.m_name.GetCStr(), descriptor.m_count, arrayIndex, descriptor.m_count);
                return false;
            }
        }
        return true;
    }

    bool ShaderResourceLayout::ValidateImageIndexAccess(ShaderInputIndex inputIndex, uint32_t arrayIndex) const
    {
        if (Validation::isEnabled)
        {
            if (!ValidateAccess(inputIndex, m_inputsForBuffers.size(), "Image"))
            {
                return false;
            }

            const ShaderInputImageDescriptor& descriptor = GetShaderInputForImage(inputIndex);
            if (arrayIndex >= descriptor.m_count)
            {
                ASSERT(false, "Image Input '{}[{}]': Array index '{}' out of range [0,{}).", descriptor.m_name.GetCStr(), descriptor.m_count, arrayIndex, descriptor.m_count);
                return false;
            }
        }
        return true;
    }

    bool ShaderResourceLayout::ValidateSamplerIndexAccess(ShaderInputIndex inputIndex, uint32_t arrayIndex) const
    {
        if (Validation::isEnabled)
        {
            if (!ValidateAccess(inputIndex, m_inputsForBuffers.size(), "Sampler"))
            {
                return false;
            }

            const ShaderInputSamplerDescriptor& descriptor = GetShaderInputForSampler(inputIndex);
            if (arrayIndex >= descriptor.m_count)
            {
                ASSERT(false, "Sampler Input '{}[{}]': Array index '{}' out of range [0,{}).", descriptor.m_name.GetCStr(), descriptor.m_count, arrayIndex, descriptor.m_count);
                return false;
            }
        }
        return true;
    }

    bool ShaderResourceLayout::ValidateBufferUnboundedArrayAccess(ShaderInputIndex inputIndex) const
    {
        return ValidateAccess(inputIndex, m_inputsForBufferUnboundedArrays.size(), "BufferUnboundedArray");
    }

    bool ShaderResourceLayout::ValidateImageUnboundedArrayAccess(ShaderInputIndex inputIndex) const
    {
        return ValidateAccess(inputIndex, m_inputsForImageUnboundedArrays.size(), "ImageUnboundedArray");
    }

    void ShaderResourceLayout::Clear()
    {
        m_staticSamplers.clear();

        m_inputsForBuffers.clear();
        m_inputsForImages.clear();
        m_inputsForSamplers.clear();

        m_intervalsForBuffers.clear();
        m_intervalsForImages.clear();
        m_intervalsForSamplers.clear();

        m_bufferSize = 0;
        m_imageSize = 0;
        m_bufferUnboundedArraySize = 0;
        m_imageUnboundedArraySize = 0;
        m_samplerSize = 0;

        m_buffersNameMap.clear();
        m_imagesNameMap.clear();
        m_samplersNameMap.clear();

        
        if (m_constantsDataLayout)
        {
            m_constantsDataLayout->Clear();
        }   

        m_bindingSlot = 0;
        m_hash = 0;
    }

    template<typename ShaderInputDescriptorType>
    bool ShaderResourceLayout::FinalizeShaderInputGroup(
        const eastl::vector<ShaderInputDescriptorType>& shaderInputDescriptors,
        eastl::vector<Interval>& intervals,
        eastl::unordered_map<ShaderInputName, ShaderInputIndex>& nameMap,
        uint32_t& size)
    {
        intervals.reserve(shaderInputDescriptors.size());
        nameMap.reserve(shaderInputDescriptors.size());

        uint32_t currentGroupSize = 0;
        uint32_t shaderInputIndex = 0;
        for (const ShaderInputDescriptorType& shaderInput : shaderInputDescriptors)
        {
            const ShaderInputIndex inputIndex(shaderInputIndex);
            if (nameMap.find(shaderInput.m_name) != nameMap.end())
            {
                Clear();
                return false;
            }

            nameMap.emplace(shaderInput.m_name, inputIndex);

            // Add the [min, max) interval for the input in the group.
            intervals.emplace_back(currentGroupSize, currentGroupSize + shaderInput.m_count);

            currentGroupSize += shaderInput.m_count;
            ++shaderInputIndex;
        }

        size = currentGroupSize;
        return true;
    }

    template<typename ShaderInputDescriptorType>
    bool ShaderResourceLayout::FinalizeUnboundedArrayShaderInputGroup(
        const eastl::vector<ShaderInputDescriptorType>& shaderInputDescriptors,
        eastl::unordered_map<ShaderInputName, ShaderInputIndex>& nameMap,
        uint32_t& size)
    {
        nameMap.Reserve(shaderInputDescriptors.size());

        uint32_t currentGroupSize = 0;
        uint32_t shaderInputIndex = 0;
        for (const ShaderInputDescriptorType& shaderInput : shaderInputDescriptors)
        {
            const ShaderInputIndex inputIndex(shaderInputIndex);
            if (nameMap.find(shaderInput.m_name) != nameMap.end())
            {
                Clear();
                return false;
            }

            nameMap.emplace(shaderInput.m_name, inputIndex);

            ++currentGroupSize;
            ++shaderInputIndex;
        }

        size = currentGroupSize;
        return true;
    }

    bool ShaderResourceLayout::IsFinalized() const
    {
        return m_hash != 0;
    }

    bool ShaderResourceLayout::Finalize()
    {
        if (IsFinalized())
        {
            return true;
        }

        if (m_bindingSlot == InvalidBindingSlot)
        {
            LOG_ERROR("[ShaderResourceGroupLayout] You must supply a valid binding slot to ShaderResourceGroupLayoutDescriptor.");
            Clear();
            return false;
        }

        if (m_bindingSlot >= Limits::Pipeline::ShaderResourceGroupCountMax)
        {
            LOG_ERROR("[ShaderResourceGroupLayout]"
                "Binding index ({}) must be less than the maximum number of allowed shader resource groups ({})",
                m_bindingSlot, Limits::Pipeline::ShaderResourceGroupCountMax);
            Clear();
            return false;
        }

        // Build buffer group
        if (!FinalizeShaderInputGroup<ShaderInputBufferDescriptor>(
            m_inputsForBuffers, m_intervalsForBuffers, m_buffersNameMap, m_bufferSize))
        {
            return false;
        }

        // Build image group
        if (!FinalizeShaderInputGroup<ShaderInputImageDescriptor>(
            m_inputsForImages, m_intervalsForImages, m_imagesNameMap, m_imageSize))
        {
            return false;
        }

        // Build sampler group
        if (!FinalizeShaderInputGroup<ShaderInputSamplerDescriptor>(
            m_inputsForSamplers, m_intervalsForSamplers, m_samplersNameMap, m_samplerSize))
        {
            return false;
        }

        // Build buffer unbounded array group
        if (!FinalizeUnboundedArrayShaderInputGroup<ShaderInputBufferUnboundedArrayDescriptor>(
            m_inputsForBufferUnboundedArrays, m_unboundedBufferArrayMap, m_bufferUnboundedArraySize))
        {
            return false;
        }

        // Build image unbounded array group
        if (!FinalizeUnboundedArrayShaderInputGroup<ShaderInputImageUnboundedArrayDescriptor>(
            m_inputsForImageUnboundedArrays, m_unboundedBufferArrayMap, m_imageUnboundedArraySize))
        {
            return false;
        }

        // Finalize the constants data layout.
        if (!m_constantsDataLayout->Finalize())
        {
            Clear();
            return false;
        }
        

        // Build the final hash based on the inputs.
        {
            size_t hash = 0;

            for (const ShaderInputStaticSamplerDescriptor& staticSamplerDescriptor : m_staticSamplers)
            {
                hash = staticSamplerDescriptor.GetHash(hash);
            }

            for (const ShaderInputBufferDescriptor& shaderInputBuffer : m_inputsForBuffers)
            {
                hash = shaderInputBuffer.GetHash(hash);
            }

            for (const ShaderInputImageDescriptor& shaderInputImage : m_inputsForImages)
            {
                hash = shaderInputImage.GetHash(hash);
            }

            for (const ShaderInputSamplerDescriptor& shaderInputSampler : m_inputsForSamplers)
            {
                hash = shaderInputSampler.GetHash(hash);
            }

            for (const ShaderInputBufferUnboundedArrayDescriptor& shaderInputBufferUnboundedArray : m_inputsForBufferUnboundedArrays)
            {
                hash = shaderInputBufferUnboundedArray.GetHash(hash);
            }

            for (const ShaderInputImageUnboundedArrayDescriptor& shaderInputImageUnboundedArray : m_inputsForImageUnboundedArrays)
            {
                hash = shaderInputImageUnboundedArray.GetHash(hash);
            }

            eastl::hash_combine(hash, m_constantsDataLayout->GetHash(), m_bindingSlot);

            m_hash = hash;
        }
        return true;
    }
}