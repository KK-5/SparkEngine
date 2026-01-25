/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <EASTL/span.h>

#include <Resource/Resource.h>
#include <Resource/Buffer/BufferView.h>
#include <Resource/Image/ImageView.h>
#include <Resource/Sampler/Sampler.h>

#include "ShaderResourceLayoutCommon.h"
#include "ConstantsData.h"
#include "ShaderResourceLayout.h"

namespace Spark::RHI
{
    enum class BindlessResourceType : uint32_t
    {
        m_Texture2D,            // ReadTexture
        m_RWTexture2D,          // ReadWriteTexture
        m_TextureCube,          // ReadTextureCube
        m_ByteAddressBuffer,    // ReadBuffer
        m_RWByteAddressBuffer,  // ReadWriteBuffer
        Count
    };

    //! This class is a platform-independent base class for a shader resource group. It has a
    //! pointer to the resource group pool, if the user initialized the group onto a pool.
    class ShaderResource : public Resource
    {
        using Base = Resource;
    public:
        //! Defines the compilation modes for an SRG
        enum class CompileMode : uint8_t
        {
            Async,  // Queues SRG compilation for later. This is the most common case.
            Sync    // Compiles the SRG immediately. To be used carefully due to performance cost.
        };

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
        bool SetImageViewArray(ShaderInputIndex inputIndex, eastl::span<const ImageView* const> imageViews, uint32_t arrayIndex = 0);

        //! Sets an unbounded array of image view for the given shader input index.
        bool SetImageViewUnboundedArray(ShaderInputIndex inputIndex, eastl::span<const ImageView* const> imageViews);

        //! Sets one buffer view for the given shader input index.
        bool SetBufferView(ShaderInputIndex inputIndex, const BufferView* bufferView, uint32_t arrayIndex = 0);

        //! Sets an array of image view for the given shader input index.
        bool SetBufferViewArray(ShaderInputIndex inputIndex, eastl::span<const BufferView* const> bufferViews, uint32_t arrayIndex = 0);

        //! Sets an unbounded array of buffer view for the given shader input index.
        bool SetBufferViewUnboundedArray(ShaderInputIndex inputIndex, eastl::span<const BufferView* const> bufferViews);

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
    private:
        static const ConstPtr<ImageView> s_nullImageView;
        static const ConstPtr<BufferView> s_nullBufferView;
        static const SamplerState s_nullSamplerState;

        ConstPtr<ShaderResourceLayout> m_shaderResourceGroupLayout;

        //! The backing data store of bound resources for the shader resource group.
        eastl::vector<ConstPtr<ImageView>> m_imageViews;
        eastl::vector<ConstPtr<BufferView>> m_bufferViews;
        eastl::vector<SamplerState> m_samplers;
        eastl::vector<ConstPtr<ImageView>> m_imageViewsUnboundedArray;
        eastl::vector<ConstPtr<BufferView>> m_bufferViewsUnboundedArray;

        //! The backing data store of constants for the shader resource group.
        ConstantsData m_constantsData;
    };
}