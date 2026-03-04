/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/span.h>
#include <Log/SpdLogSystem.h>

#include <RHI/Resource/Resource.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include "ShaderResourceLayoutCommon.h"
#include "ConstantsData.h"
#include "ShaderResourceLayout.h"

namespace Spark::RHI
{
    //! This class is a platform-independent base class for a shader resource group. It has a
    //! pointer to the resource group pool, if the user initialized the group onto a pool.
    class ShaderResource : public Resource
    {
        friend class ShaderResourcePool;
    public:
        virtual ~ShaderResource() = default;

        //! Defines the compilation modes for an SRG
        enum class CompileMode : uint8_t
        {
            Async,  // Queues SRG compilation for later. This is the most common case.
            Sync    // Compiles the SRG immediately. To be used carefully due to performance cost.
        };

        //! Returns the shader resource group pool that this group is registered on.
        ShaderResourcePool* GetPool();
        const ShaderResourcePool* GetPool() const;

        //! Returns the binding slot specified by the layout associated to this shader resource group.
        uint32_t GetBindingSlot() const { return m_bindingSlot; }

        //! Resolves a shader input name to an index using reflection. For performance reasons, this resolve
        //! operation should be performed once at initialization time (or as infrequently as possible).
        //! Assignment of shader inputs is faster when done using the shader input index directly.
        ShaderInputIndex FindShaderInputBufferIndex(const ShaderInputName& name) const;
        ShaderInputIndex FindShaderInputImageIndex(const ShaderInputName& name) const;
        ShaderInputIndex FindShaderInputSamplerIndex(const ShaderInputName& name) const;
        ShaderInputIndex FindShaderInputConstantIndex(const ShaderInputName& name) const;

        //! Sets one image view for the given shader input index.
        bool SetImageView(ShaderInputIndex inputIndex, const ImageView* imageView, uint32_t arrayIndex);

        //! Sets an array of image view for the given shader input index.
        bool SetImageViewArray(ShaderInputIndex inputIndex, eastl::span<const ImageView*> imageViews, uint32_t arrayIndex = 0);

        //! Sets an unbounded array of image view for the given shader input index.
        bool SetImageViewUnboundedArray(ShaderInputIndex inputIndex, eastl::span<const ImageView*> imageViews);

        //! Sets one buffer view for the given shader input index.
        bool SetBufferView(ShaderInputIndex inputIndex, const BufferView* bufferView, uint32_t arrayIndex = 0);

        //! Sets an array of image view for the given shader input index.
        bool SetBufferViewArray(ShaderInputIndex inputIndex, eastl::span<const BufferView*> bufferViews, uint32_t arrayIndex = 0);

        //! Sets an unbounded array of buffer view for the given shader input index.
        bool SetBufferViewUnboundedArray(ShaderInputIndex inputIndex, eastl::span<const BufferView*> bufferViews);

        //! Sets one sampler for the given shader input index, using the bindingIndex as the key.
        bool SetSampler(ShaderInputIndex inputIndex, const SamplerState& sampler, uint32_t arrayIndex = 0);

        //! Sets an array of samplers for the given shader input index.
        bool SetSamplerArray(ShaderInputIndex inputIndex, eastl::span<const SamplerState> samplers, uint32_t arrayIndex = 0);

        //! Assigns constant data for the given constant shader input index.
        bool SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, uint32_t byteCount);
        bool SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, uint32_t byteOffset, uint32_t byteCount);

        //! Assigns constant data as a whole.
        bool SetConstantData(const void* bytes, uint32_t byteCount);
        bool SetConstantData(const void* bytes, uint32_t byteOffset, uint32_t byteCount);

        //! Returns a single image view associated with the image shader input index and array offset.
        const ConstPtr<ImageView>& GetImageView(ShaderInputIndex inputIndex, uint32_t arrayIndex) const;

        //! Returns a span of image views associated with the given image shader input index.
        eastl::span<const ConstPtr<ImageView>> GetImageViewArray(ShaderInputIndex inputIndex) const;

        //! Returns an unbounded span of image views associated with the given buffer shader input index.
        eastl::span<const ConstPtr<ImageView>> GetImageViewUnboundedArray(ShaderInputIndex inputIndex) const;

        //! Returns a single buffer view associated with the buffer shader input index and array offset.
        const ConstPtr<BufferView>& GetBufferView(ShaderInputIndex inputIndex, uint32_t arrayIndex) const;

        //! Returns a span of buffer views associated with the given buffer shader input index.
        eastl::span<const ConstPtr<BufferView>> GetBufferViewArray(ShaderInputIndex inputIndex) const;

        //! Returns an unbounded span of buffer views associated with the given buffer shader input index.
        eastl::span<const ConstPtr<BufferView>> GetBufferViewUnboundedArray(ShaderInputIndex inputIndex) const;

        //! Returns a single sampler associated with the sampler shader input index and array offset.
        const SamplerState& GetSampler(ShaderInputIndex inputIndex, uint32_t arrayIndex) const;

        //! Returns a span of samplers associated with the sampler shader input index.
        eastl::span<const SamplerState> GetSamplerArray(ShaderInputIndex inputIndex) const;

        //! Returns constant data for the given shader input index as a span of bytes.
        eastl::span<const uint8_t> GetConstantRaw(ShaderInputIndex inputIndex) const;

        //! Returns a {Buffer, Image, Sampler} shader resource group. Each resource type has its own separate group.
        //!  - The size of this group matches the size provided by ShaderResourceGroupLayout::GetGroupSizeFor{Buffer, Image, Sampler}.
        //!  - Use ShaderResourceGroupLayout::GetGroupInterval to retrieve a [min, max) interval into the span.
        eastl::span<const ConstPtr<ImageView>> GetImageGroup() const;
        eastl::span<const ConstPtr<BufferView>> GetBufferGroup() const;
        eastl::span<const SamplerState> GetSamplerGroup() const;

        //! Reset image and buffer views setup for this DeviceShaderResourceGroupData
        //! So it won't hold references for any RHI resources
        void ResetViews();

        //! Returns the opaque constant data populated by calls to SetConstant and SetConstantData.
        //!
        //! CAUTION!
        //! Different platforms might follow different packing rules for the internally-managed SRG constant buffer.
        eastl::span<const uint8_t> GetConstantData() const;

        //! Returns the underlying ConstantsData struct
        const ConstantsData& GetConstantsData() const;

        //! Returns the shader resource layout for this group.
        const ShaderResourceLayout* GetLayout() const;

    protected:
        ShaderResource() = default;

        virtual void SetLayout(const ShaderResourceLayout* layout);
    private:
        static const ConstPtr<ImageView> s_nullImageView;
        static const ConstPtr<BufferView> s_nullBufferView;
        static const SamplerState s_nullSamplerState;

        bool ValidateSetImageView(ShaderInputIndex inputIndex, const ImageView* imageView, uint32_t arrayIndex) const;
        bool ValidateSetBufferView(ShaderInputIndex inputIndex, const BufferView* bufferView, uint32_t arrayIndex) const;

        bool ValidateImageViewAccess(ShaderInputIndex inputIndex, const ImageView* imageView, uint32_t arrayIndex) const;
        bool ValidateBufferViewAccess(ShaderInputIndex inputIndex, const BufferView* bufferView, uint32_t arrayIndex) const;

        ConstPtr<ShaderResourceLayout> m_shaderResourceGroupLayout;

        //! The backing data store of bound resources for the shader resource group.
        eastl::vector<ConstPtr<ImageView>> m_imageViews;
        eastl::vector<ConstPtr<BufferView>> m_bufferViews;
        eastl::vector<SamplerState> m_samplers;
        eastl::vector<ConstPtr<ImageView>> m_imageViewsUnboundedArray;
        eastl::vector<ConstPtr<BufferView>> m_bufferViewsUnboundedArray;

        //! The backing data store of constants for the shader resource group.
        ConstantsData m_constantsData;

        // The binding slot cached from the layout.
        uint32_t m_bindingSlot = static_cast<uint32_t>(-1);
    };

    /*
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
        const BufferFrameAttachment* frameAttachment = buffer.GetFrameAttachment();

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
            if (!frameAttachment)
            {
                LOG_ERROR("[ShaderResource]"
                    "Buffer Input '{}[{}]': DeviceBuffer is bound to a ReadWrite shader input, "
                    "but it is not an attachment on the frame scheduler. All GPU-writable resources "
                    "must be declared as attachments in order to provide hazard tracking.",
                    shaderInputBuffer.m_name.GetCStr(), arrayIndex);
                return false;
            }

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
    */
}