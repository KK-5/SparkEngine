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
    const ConstPtr<ImageView> ShaderResource::s_nullImageView;
    const ConstPtr<BufferView> ShaderResource::s_nullBufferView;
    const SamplerState ShaderResource::s_nullSamplerState{};

    bool ShaderResource::ValidateImageViewAccess(ShaderInputIndex inputIndex, const ImageView* imageView, uint32_t arrayIndex) const
    {
        if (!Validation::isEnabled)
        {
            return true;
        }

        const ShaderInputImageDescriptor shaderInputImage = GetLayout()->GetShaderInputForImage(inputIndex);

        if (!imageView)
        {
            LOG_ERROR("[ShaderResource]"
                "Image Array Input '{}[{}]' is null.",
                shaderInputImage.m_name.GetCStr(), arrayIndex);
            return false;
        }

        const ImageViewDescriptor& imageViewDescriptor = imageView->GetDescriptor();
        const Image& image = imageView->GetImage();
        const ImageDescriptor& imageDescriptor = image.GetDescriptor();
        const ImageFrameAttachment* frameAttachment = image.GetFrameAttachment();

        // The image must have the correct bind flags for the slot.
        const bool isValidAccess =
            (shaderInputImage.m_access == ShaderInputImageAccess::Read && CheckBitsAll(imageDescriptor.m_bindFlags, ImageBindFlags::ShaderRead)) ||
            (shaderInputImage.m_access == ShaderInputImageAccess::ReadWrite && CheckBitsAll(imageDescriptor.m_bindFlags, ImageBindFlags::ShaderReadWrite));
        
        if (!isValidAccess)
        {
            LOG_ERROR("[ShaderResource]"
                "Image Input '{}[{}]': Invalid 'Read / Write' access.",
                shaderInputImage.m_name.GetCStr(), arrayIndex);
            return false;

            // NOTE: We aren't able to validate the scope attachment here, because shader resource groups aren't directly
            // associated with a scope. Instead, the CommandListValidator class will check that the access is correct at
            // command list submission time.
        }

        auto checkImageType = [&imageDescriptor, &shaderInputImage, arrayIndex](ImageDimension expected)
        {
            if (imageDescriptor.m_dimension != expected)
            {
                (void)(shaderInputImage);
                (void)(arrayIndex);
                LOG_ERROR("[ShaderResource]",
                    "Image Input '{}[{}]': The image is {}D but the shader expected {}D",
                    shaderInputImage.m_name.GetCStr(),
                    arrayIndex,
                    static_cast<int>(imageDescriptor.m_dimension),
                    static_cast<int>(expected));
                return false;
            }
            return true;
        };

        switch (shaderInputImage.m_type)
        {
        case ShaderInputImageType::Unknown:
            // Unable to validate.
            break;

        case ShaderInputImageType::Image1DArray:
        case ShaderInputImageType::Image1D:
            if (!checkImageType(ImageDimension::Image1D))
            {
                return false;
            }
            break;

        case ShaderInputImageType::SubpassInput:
            if (!checkImageType(ImageDimension::Image2D))
            {
                return false;
            }
            break;

        case ShaderInputImageType::Image2DArray:
        case ShaderInputImageType::Image2D:
            if (!checkImageType(ImageDimension::Image2D))
            {
                return false;
            }
            if (imageDescriptor.m_multisampleState.m_samples != 1)
            {
                LOG_ERROR("[ShaderResource]",
                    "Image Input '{}[{}]': The image has multisample count {} but the shader expected 1.",
                    shaderInputImage.m_name.GetCStr(),
                    arrayIndex,
                    imageDescriptor.m_multisampleState.m_samples);
                return false;
            }
            break;

        case ShaderInputImageType::Image2DMultisample:
        case ShaderInputImageType::Image2DMultisampleArray:
            if (!checkImageType(ImageDimension::Image2D))
            {
                return false;
            }
            if (imageDescriptor.m_multisampleState.m_samples <= 1)
            {
                LOG_ERROR("[ShaderResource]"
                    "Image Input '{}[{}]': The image has multisample count {} but the shader expected more than 1.",
                    shaderInputImage.m_name.GetCStr(),
                    arrayIndex,
                    imageDescriptor.m_multisampleState.m_samples);
                return false;
            }
            break;

        case ShaderInputImageType::Image3D:
            if (!checkImageType(ImageDimension::Image3D))
            {
                return false;
            }
            break;

        case ShaderInputImageType::ImageCube:
        case ShaderInputImageType::ImageCubeArray:
            if (!checkImageType(ImageDimension::Image2D))
            {
                return false;
            }
            if (imageViewDescriptor.m_isCubemap == 0)
            {
                LOG_ERROR("[ShaderResource]"
                    "Image Input '{}[{}]': The shader expected a cubemap.",
                    shaderInputImage.m_name.GetCStr(),
                    arrayIndex);
                return false;
            }
            break;

        default:
            ASSERT(false, "Image Input '{}[{}]': Invalid image type!", shaderInputImage.m_name.GetCStr(), arrayIndex);
            return false;
        }

        return true;
    }

    bool ShaderResource::ValidateBufferViewAccess(ShaderInputIndex inputIndex, const BufferView* bufferView, [[maybe_unused]] uint32_t arrayIndex) const
    {
        if (!Validation::isEnabled)
        {
            return true;
        }

        const ShaderInputBufferDescriptor& shaderInputBuffer = GetLayout()->GetShaderInputForBuffer(inputIndex);
        const BufferViewDescriptor& bufferViewDescriptor = bufferView->GetDescriptor();
        const Buffer& buffer = bufferView->GetBuffer();
        const BufferDescriptor& bufferDescriptor = buffer.GetDescriptor();

        const bool isValidAccess =
            (shaderInputBuffer.m_access == ShaderInputBufferAccess::Constant && CheckBitsAll(bufferDescriptor.m_bindFlags, BufferBindFlags::Constant)) ||
            (shaderInputBuffer.m_access == ShaderInputBufferAccess::Read && CheckBitsAll(bufferDescriptor.m_bindFlags, BufferBindFlags::ShaderRead)) ||
            (shaderInputBuffer.m_access == ShaderInputBufferAccess::Read && CheckBitsAll(bufferDescriptor.m_bindFlags, BufferBindFlags::RayTracingAccelerationStructure)) ||
            (shaderInputBuffer.m_access == ShaderInputBufferAccess::ReadWrite && CheckBitsAll(bufferDescriptor.m_bindFlags, BufferBindFlags::ShaderReadWrite));

        if (!isValidAccess)
        {
            LOG_ERROR("[ShaderResource]"
                "Buffer Input '{}[{}]': Invalid 'Constant / Read / Write' access.",
                shaderInputBuffer.m_name.GetCStr(), arrayIndex);
            return false;
        }

        if (shaderInputBuffer.m_access == ShaderInputBufferAccess::ReadWrite)
        {
            // A buffer view assigned to an input with read-write access must be an attachment on the frame scheduler.

            // NOTE: We aren't able to validate the scope attachment here, because shader resource groups aren't directly
            // associated with a scope. Instead, the CommandListValidator class will check that the access is correct at
            // command list submission time.
        }

        if (shaderInputBuffer.m_type == ShaderInputBufferType::Constant)
        {
            //For Constant type the stride (full constant buffer) must be larger or equal than the buffer view size (element size x element count).
            if (!(shaderInputBuffer.m_strideSize >= (bufferViewDescriptor.m_elementSize * bufferViewDescriptor.m_elementCount)))
            {
                LOG_ERROR("[ShaderResource]"
                    "Buffer Input '{}[{}]': stride size {} must be larger than or equal to the buffer view size {}",
                    shaderInputBuffer.m_name.GetCStr(), arrayIndex, shaderInputBuffer.m_strideSize,
                    (bufferViewDescriptor.m_elementSize * bufferViewDescriptor.m_elementCount));
                return false;
            }
        }
        else if (shaderInputBuffer.m_type != ShaderInputBufferType::Typed)
        {
            // For any other type the buffer view's element size should match the stride.
            if (shaderInputBuffer.m_strideSize != bufferViewDescriptor.m_elementSize)
            {
                LOG_ERROR(
                    "[ShaderResource]"
                    "Buffer Input '{}[{}]': Does not match expected stride size. Buffer has stride size {}. SRG expects stride size {}",
                    shaderInputBuffer.m_name.GetCStr(),
                    arrayIndex,
                    bufferViewDescriptor.m_elementSize,
                    shaderInputBuffer.m_strideSize);
                return false;
            }
        }

        bool isValidType = true;
        switch (shaderInputBuffer.m_type)
        {
        case ShaderInputBufferType::Unknown:
            // Unable to validate.
            break;

        case ShaderInputBufferType::Constant:
            // Element format is not relevant for viewing a buffer as a constant buffer, any format would be valid.
            break;

        case ShaderInputBufferType::Structured:
            isValidType &= bufferViewDescriptor.m_elementFormat == Format::Unknown;
            break;

        case ShaderInputBufferType::Typed:
            isValidType &= bufferViewDescriptor.m_elementFormat != Format::Unknown;
            break;

        case ShaderInputBufferType::Raw:
            isValidType &= bufferViewDescriptor.m_elementFormat == Format::R32_UINT;
            break;

        case ShaderInputBufferType::AccelerationStructure:
            isValidType &= bufferViewDescriptor.m_elementFormat == Format::R32_UINT;
            break;

        default:
            ASSERT(false, "Buffer Input '{}[{}]': Invalid buffer type!", shaderInputBuffer.m_name.GetCStr(), arrayIndex);
            return false;
        }

        if (!isValidType)
        {
            LOG_ERROR("[ShaderResource]"
                "Buffer Input '{}[{}]': Does not match expected type",
                shaderInputBuffer.m_name.GetCStr(), arrayIndex);
            return false;
        }

        return true;
    }
    
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

    ResultCode ShaderResource::Init(Device& device, ConstPtr<ShaderResourceLayout> layout)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[ShaderResource] ShaderResource is already initialized.");
                return ResultCode::InvalidOperation;
            }

            if (!layout)
            {
                LOG_ERROR("[ShaderResource] ShaderResourceLayout is null");
                return ResultCode::InvalidArgument;
            }
        }

        SetLayout(layout.get());
        DeviceObject::Init(device);
        return ResultCode::Success;
    }

    void ShaderResource::Shutdown()
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[ShaderResource] ShaderResource is not initialized.");
                return;
            }
        }

        ResetViews();
        ShutdownInternal();
        DeviceObject::Shutdown();
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
        m_bindingSlot = layout->GetBindingSlot();

        m_imageViews.resize(layout->GetImagesSize());
        m_bufferViews.resize(layout->GetBuffersSize());
        m_samplers.resize(layout->GetSamplersSize());

        const ConstantsLayout* constantsLayout = layout->GetConstantsLayout();
        m_constantsData = ConstantsData(constantsLayout);
    }

    const ShaderResourceLayout* ShaderResource::GetLayout() const
    {
        return m_shaderResourceGroupLayout.get();
    }
}