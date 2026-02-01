/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderResource.h"

#include "ShaderResourcePool.h"

namespace Spark::RHI
{
    bool ShaderResource::ValidateSetImageView(ShaderInputIndex inputIndex, const ImageView* imageView, uint32_t arrayIndex) const
    {
        if (!Validation::isEnabled)
        {
            return true;
        }
        if (!GetLayout()->ValidateImageIndexAccess(inputIndex, arrayIndex))
        {
            return false;
        }

        if (imageView)
        {
            if (!ValidateImageViewAccess(inputIndex, imageView, arrayIndex))
            {
                return false;
            }
        }

        return true;
    }

    bool ShaderResource::ValidateSetBufferView(ShaderInputIndex inputIndex, const BufferView* bufferView, uint32_t arrayIndex) const
    {
        if (!Validation::isEnabled)
        {
            return true;
        }
        if (!GetLayout()->ValidateBufferIndexAccess(inputIndex, arrayIndex))
        {
            return false;
        }

        if (bufferView)
        {
            if (!ValidateBufferViewAccess(inputIndex, bufferView, arrayIndex))
            {
                return false;
            }
        }

        return true;
    }

    ShaderInputIndex ShaderResource::FindShaderInputBufferIndex(const ShaderInputName& name) const
    {
        return m_shaderResourceGroupLayout->FindShaderInputBufferIndex(name);
    }

    ShaderInputIndex ShaderResource::FindShaderInputImageIndex(const ShaderInputName& name) const
    {
        return m_shaderResourceGroupLayout->FindShaderInputImageIndex(name);
    }

    ShaderInputIndex ShaderResource::FindShaderInputSamplerIndex(const ShaderInputName& name) const
    {
        return m_shaderResourceGroupLayout->FindShaderInputSamplerIndex(name);
    }

    ShaderInputIndex ShaderResource::FindShaderInputConstantIndex(const ShaderInputName& name) const
    {
        return m_shaderResourceGroupLayout->FindShaderInputConstantIndex(name);
    }

    bool ShaderResource::SetImageView(ShaderInputIndex inputIndex, const ImageView* imageView, uint32_t arrayIndex)
    {
        eastl::array<const ImageView*, 1> imageViews = {{imageView}};
        return SetImageViewArray(inputIndex, imageViews, arrayIndex);
    }

    bool ShaderResource::SetImageViewArray(ShaderInputIndex inputIndex, eastl::span<const ImageView*> imageViews, uint32_t arrayIndex)
    {
        if (GetLayout()->ValidateImageIndexAccess(inputIndex, static_cast<uint32_t>(arrayIndex + imageViews.size() - 1)))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForImage(inputIndex);
            bool isValidAll = true;
            for (size_t i = 0; i < imageViews.size(); ++i)
            {
                const bool isValid = ValidateSetImageView(inputIndex, imageViews[i], static_cast<uint32_t>(arrayIndex + i));
                if (isValid)
                {
                    m_imageViews[interval.m_min + arrayIndex + i] = imageViews[i];
                }
                isValidAll &= isValid;
            }

            return isValidAll;
        }
        return false;
    }

    bool ShaderResource::SetImageViewUnboundedArray(ShaderInputIndex inputIndex, eastl::span<const ImageView*> imageViews)
    {
        /*
        if (GetLayout()->ValidateImageUnboundedArrayAccess(inputIndex))
        {
            m_imageViewsUnboundedArray.clear();
            bool isValidAll = true;
            for (size_t i = 0; i < imageViews.size(); ++i)
            {
                bool isValid = true;
                if (imageViews[i])
                {
                    isValid = ValidateImageViewAccess<ShaderInputImageUnboundedArrayDescriptor>(
                        inputIndex, imageViews[i], static_cast<uint32_t>(i));
                }
                if (isValid)
                {
                    m_imageViewsUnboundedArray.push_back(imageViews[i]);
                }
                isValidAll &= isValid;
            }

            return isValidAll;
        }
        */
        return false;
    }

    bool ShaderResource::SetBufferView(ShaderInputIndex inputIndex, const BufferView* bufferView, uint32_t arrayIndex)
    {
        eastl::array<const BufferView*, 1> bufferViews = {{bufferView}};
        return SetBufferViewArray(inputIndex, bufferViews, arrayIndex);
    }

    bool ShaderResource::SetBufferViewArray(ShaderInputIndex inputIndex, eastl::span<const BufferView*> bufferViews, uint32_t arrayIndex)
    {
        if (GetLayout()->ValidateBufferIndexAccess(inputIndex, static_cast<uint32_t>(arrayIndex + bufferViews.size() - 1)))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForBuffer(inputIndex);
            bool isValidAll = true;
            for (size_t i = 0; i < bufferViews.size(); ++i)
            {
                const bool isValid = ValidateSetBufferView(inputIndex, bufferViews[i], arrayIndex);
                if (isValid)
                {
                    m_bufferViews[interval.m_min + arrayIndex + i] = bufferViews[i];
                }
                isValidAll &= isValid;
            }

            return isValidAll;
        }
        return false;
    }

    bool ShaderResource::SetBufferViewUnboundedArray(ShaderInputIndex inputIndex, eastl::span<const BufferView*> bufferViews)
    {
        /*
        if (GetLayout()->ValidateBufferUnboundedArrayAccess(inputIndex))
        {
            m_bufferViewsUnboundedArray.clear();
            bool isValidAll = true;
            for (size_t i = 0; i < bufferViews.size(); ++i)
            {
                bool isValid = true;
                if (bufferViews[i])
                {
                    isValid = ValidateBufferViewAccess<ShaderInputBufferUnboundedArrayDescriptor>(
                        inputIndex, bufferViews[i], static_cast<uint32_t>(i));
                }
                if (isValid)
                {
                    m_bufferViewsUnboundedArray.push_back(bufferViews[i]);
                }
                isValidAll &= isValid;
            }

            return isValidAll;
        }
        */
        return false;
    }

    bool ShaderResource::SetSampler(ShaderInputIndex inputIndex, const SamplerState& sampler, uint32_t arrayIndex)
    {
        return SetSamplerArray(inputIndex, eastl::span<const SamplerState>(&sampler, 1), arrayIndex);
    }

    bool ShaderResource::SetSamplerArray(ShaderInputIndex inputIndex, eastl::span<const SamplerState> samplers, uint32_t arrayIndex)
    {
        if (GetLayout()->ValidateSamplerIndexAccess(inputIndex, static_cast<uint32_t>(arrayIndex + samplers.size() - 1)))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForSampler(inputIndex);
            for (size_t i = 0; i < samplers.size(); ++i)
            {
                m_samplers[interval.m_min + arrayIndex + i] = samplers[i];
            }
            return true;
        }
        return false;
    }

    bool ShaderResource::SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, uint32_t byteCount)
    {
        return SetConstantRaw(inputIndex, bytes, 0, byteCount);
    }

    bool ShaderResource::SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, uint32_t byteOffset, uint32_t byteCount)
    {
        return m_constantsData.SetConstantRaw(inputIndex, bytes, byteOffset, byteCount);
    }

    bool ShaderResource::SetConstantData(const void* bytes, uint32_t byteCount)
    {
        return SetConstantData(bytes, 0, byteCount);
    }

    bool ShaderResource::SetConstantData(const void* bytes, uint32_t byteOffset, uint32_t byteCount)
    {
        return m_constantsData.SetConstantData(bytes, 0, byteCount);
    }

    const ConstPtr<ImageView>& ShaderResource::GetImageView(ShaderInputIndex inputIndex, uint32_t arrayIndex) const
    {
        if (GetLayout()->ValidateImageIndexAccess(inputIndex, arrayIndex))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForImage(inputIndex);
            return m_imageViews[interval.m_min + arrayIndex];
        }
        return s_nullImageView;
    }

    eastl::span<const ConstPtr<ImageView>> ShaderResource::GetImageViewArray(ShaderInputIndex inputIndex) const
    {
        if (GetLayout()->ValidateImageIndexAccess(inputIndex, 0))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForImage(inputIndex);
            return eastl::span<const ConstPtr<ImageView>>(&m_imageViews[interval.m_min], interval.m_max - interval.m_min);
        }
        return {};
    }

    eastl::span<const ConstPtr<ImageView>> ShaderResource::GetImageViewUnboundedArray(ShaderInputIndex inputIndex) const
    {
        if (GetLayout()->ValidateImageUnboundedArrayAccess(inputIndex))
        {
            return eastl::span<const ConstPtr<ImageView>>(m_imageViewsUnboundedArray.data(), m_imageViewsUnboundedArray.size());
        }
        return {};
    }

    const ConstPtr<BufferView>& ShaderResource::GetBufferView(ShaderInputIndex inputIndex, uint32_t arrayIndex) const
    {
        if (GetLayout()->ValidateBufferIndexAccess(inputIndex, arrayIndex))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForBuffer(inputIndex);
            return m_bufferViews[interval.m_min + arrayIndex];
        }
        return s_nullBufferView;
    }

    eastl::span<const ConstPtr<BufferView>> ShaderResource::GetBufferViewArray(ShaderInputIndex inputIndex) const
    {
        if (GetLayout()->ValidateBufferIndexAccess(inputIndex, 0))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForBuffer(inputIndex);
            return eastl::span<const ConstPtr<BufferView>>(&m_bufferViews[interval.m_min], interval.m_max - interval.m_min);
        }
        return {};
    }

    eastl::span<const ConstPtr<BufferView>> ShaderResource::GetBufferViewUnboundedArray(ShaderInputIndex inputIndex) const
    {
        if (GetLayout()->ValidateBufferUnboundedArrayAccess(inputIndex))
        {
            return eastl::span<const ConstPtr<BufferView>>(m_bufferViewsUnboundedArray.data(), m_bufferViewsUnboundedArray.size());
        }
        return {};
    }

    const SamplerState& ShaderResource::GetSampler(ShaderInputIndex inputIndex, uint32_t arrayIndex) const
    {
        if (GetLayout()->ValidateSamplerIndexAccess(inputIndex, arrayIndex))
        {
            const Interval interval = GetLayout()->GetGroupIntervalForSampler(inputIndex);
            return m_samplers[interval.m_min + arrayIndex];
        }
        return s_nullSamplerState;
    }

    eastl::span<const SamplerState> ShaderResource::GetSamplerArray(ShaderInputIndex inputIndex) const
    {
        const Interval interval = GetLayout()->GetGroupIntervalForSampler(inputIndex);
        return eastl::span<const SamplerState>(&m_samplers[interval.m_min], interval.m_max - interval.m_min);
    }

    eastl::span<const uint8_t> ShaderResource::GetConstantRaw(ShaderInputIndex inputIndex) const
    {
        return m_constantsData.GetConstantRaw(inputIndex);
    }

    eastl::span<const ConstPtr<ImageView>> ShaderResource::GetImageGroup() const
    {
        return m_imageViews;
    }

    eastl::span<const ConstPtr<BufferView>> ShaderResource::GetBufferGroup() const
    {
        return m_bufferViews;
    }

    eastl::span<const SamplerState> ShaderResource::GetSamplerGroup() const
    {
        return m_samplers;
    }

    void ShaderResource::ResetViews()
    {
        m_imageViews.assign(m_imageViews.size(), nullptr);
        m_bufferViews.assign(m_bufferViews.size(), nullptr);
        m_imageViewsUnboundedArray.assign(m_imageViewsUnboundedArray.size(), nullptr);
        m_bufferViewsUnboundedArray.assign(m_bufferViewsUnboundedArray.size(), nullptr);
    }

    eastl::span<const uint8_t> ShaderResource::GetConstantData() const
    {
        return m_constantsData.GetConstantData();
    }

    const ConstantsData& ShaderResource::GetConstantsData() const
    {
        return m_constantsData;
    }

    void ShaderResource::SetLayout(const ShaderResourceLayout* layout)
    {
        m_shaderResourceGroupLayout = layout;
    }

    const ShaderResourceLayout* ShaderResource::GetLayout() const
    {
        return m_shaderResourceGroupLayout.get();
    }

    ShaderResourcePool* ShaderResource::GetPool()
    {
        return static_cast<ShaderResourcePool*>(Resource::GetPool());
    }

    const ShaderResourcePool* ShaderResource::GetPool() const
    {
        return static_cast<const ShaderResourcePool*>(Resource::GetPool());
    }
}