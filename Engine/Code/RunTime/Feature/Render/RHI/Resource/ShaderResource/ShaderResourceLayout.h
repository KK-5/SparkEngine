/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Simplify all types of ShaderInputIndex to uint32_t
 */

#pragma once

#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/span.h>
#include <Math/Interval.h>
#include <Object/Object.h>
#include "ShaderResourceDescriptor.h"
#include "ShaderResourceLayoutCommon.h"
#include "ConstantsLayout.h"

namespace Spark::RHI
{
    class ShaderResourceLayout final : public Object
    {
    public:
        //! Adds a static sampler to the layout. Static samplers are immutable and cannot
        //! be assigned at runtime.
        void AddStaticSampler(const ShaderInputStaticSamplerDescriptor& sampler);

        //! Adds a shader input to the shader resource group layout. The layout maintains
        //! a separate list for each type of shader input. Order in each list is preserved.
        void AddShaderInput(const ShaderInputBufferDescriptor& buffer);
        void AddShaderInput(const ShaderInputImageDescriptor& image);
        void AddShaderInput(const ShaderInputBufferUnboundedArrayDescriptor& bufferUnboundedArray);
        void AddShaderInput(const ShaderInputImageUnboundedArrayDescriptor& imageUnboundedArray);
        void AddShaderInput(const ShaderInputSamplerDescriptor& sampler);
        void AddShaderInput(const ShaderInputConstantDescriptor& constant);

        //! Assigns the shader resource group binding slot to the layout. The binding slot is
        //! defined by the shader content and dictates which logical slot to use when binding
        //! shader resource groups to command lists.
        void SetBindingSlot(uint32_t bindingSlot);

        //! Returns the binding slot allocated to this layout. Each layout occupies a logical binding slot
        //! on a command list. All shader resource groups which use the layout are bound to that slot.
        uint32_t GetBindingSlot() const;

        //! Clears the layout to an empty state. The layout must be finalized prior to usage.
        void Clear();

        bool IsFinalized() const;

        //! Finalizes the layout for access. Must be called prior to usage or serialization.
        //! It is not permitted to mutate the layout once Finalize is called. Clear must
        //! be called first. If the call fails, the layout is cleared back to an empty state
        //! and false is returned. Otherwise, true is returned and the layout is considered
        //! finalized.
        bool Finalize();

        //////////////////////////////////////////////////////////////////////////
        // The following methods are only permitted on a finalized layout.

        /// Returns the full list of static samplers descriptors declared on the layout.
        eastl::span<const ShaderInputStaticSamplerDescriptor> GetStaticSamplers() const;

        //! Resolves an shader input name to an index for each type of shader input. To maximize performance,
        //! the name to index resolve should be done as an initialization step and the indices
        //! cached.
        ShaderInputIndex  FindShaderInputBufferIndex(const ShaderInputName& name) const;
        ShaderInputIndex  FindShaderInputImageIndex(const ShaderInputName& name) const;
        ShaderInputIndex  FindShaderInputSamplerIndex(const ShaderInputName& name) const;
        ShaderInputIndex  FindShaderInputConstantIndex(const ShaderInputName& name) const;

        ShaderInputIndex FindShaderInputBufferUnboundedArrayIndex(const ShaderInputName& name) const;
        ShaderInputIndex FindShaderInputImageUnboundedArrayIndex(const ShaderInputName& name) const;

        //! Returns the shader input associated with the requested index. It is not permitted
        //! to call this method with a null index. An assert will fire otherwise.
        const ShaderInputBufferDescriptor&    GetShaderInputForBuffer(ShaderInputIndex index) const;
        const ShaderInputImageDescriptor&     GetShaderInputForImage(ShaderInputIndex index) const;
        const ShaderInputSamplerDescriptor&   GetShaderInputForSampler(ShaderInputIndex index) const;
        const ShaderInputConstantDescriptor&  GetShaderInputForConstant(ShaderInputIndex index) const;

        const ShaderInputBufferUnboundedArrayDescriptor& GetShaderInputForBufferUnboundedArray(ShaderInputIndex index) const;
        const ShaderInputImageUnboundedArrayDescriptor& GetShaderInputForImageUnboundArray(ShaderInputIndex index) const;

        //! Returns the full lists of each kind of shader input added to the layout. Inputs
        //! maintain their original order with respect to AddShaderInput. Each type
        //! of shader input has its own separate list.
        eastl::span<const ShaderInputBufferDescriptor>   GetShaderInputListForBuffers() const;
        eastl::span<const ShaderInputImageDescriptor>    GetShaderInputListForImages() const;
        eastl::span<const ShaderInputSamplerDescriptor>  GetShaderInputListForSamplers() const;
        eastl::span<const ShaderInputConstantDescriptor> GetShaderInputListForConstants() const;

        eastl::span<const ShaderInputBufferUnboundedArrayDescriptor> GetShaderInputListForBufferUnboundedArrays() const;
        eastl::span<const ShaderInputImageUnboundedArrayDescriptor>  GetShaderInputListForImageUnboundedArrays() const;

         //! Each shader input may contain multiple shader resources. The layout computes
        //! a separate shader resource group for each resource type. For example, given
        //! shader inputs 'A', 'B' and 'C':
        //!
        //!  Shader Input List:       (A)       (B)    (C)
        //!  Shader Resource Group:   [0, 1, 2] [3, 4] [5]
        //!
        //! [A, B, C] form a list of three shader inputs. But the shader resource group
        //! forms a group of six resources. The following methods provide a mapping from
        //! a shader input index to an interval of resources in the resource group.
        //!
        //! The following methods return an interval - [min, max) - into the shader resource
        //! group for that resource type.
        Interval GetGroupInterval(ShaderInputIndex inputIndex) const;
        Interval GetGroupInterval(ShaderInputIndex inputIndex) const;
        Interval GetGroupInterval(ShaderInputIndex inputIndex) const;

        //! The interval of a constant is its byte [min, max) into the constant data.
        //Interval GetConstantInterval(ShaderInputIndex inputIndex) const;

        //! Returns the total size of the flat resource table for each type of resource.
        //! Note that this size is not 1-to-1 with the 'shader input list' for that type
        //! of resource, since a shader input may be an array of resources.
        //!
        //! NOTE: The resource table maps to the following types per resource:
        //!  - Buffer:   BufferView
        //!  - Image:    ImageView
        //!  - Sampler:  SamplerState
        uint32_t GetBufferSize() const;
        uint32_t GetImageSize() const;
        uint32_t GetBufferUnboundedArraySize() const;
        uint32_t GetImageUnboundedArraySize() const;
        uint32_t GetSamplersSize() const;

        //! Constants are different and live in an opaque buffer of bytes instead of a resource group.
        //uint32_t GetConstantDataSize() const;

        //! Returns the constants data layout;
        //const ConstantsLayout* GetConstantsLayout() const;

        //! Returns the hash computed in Finalize.
        size_t GetHash() const;

        //! Validates that the inputIndex is valid.
        //! Emits an assert and returns false on failure; returns true on success. If validation is disabled true is always returned.
        //bool ValidateAccess(RHI::ShaderInputConstantIndex inputIndex) const;

        //! Validates that the inputIndex is valid and the arrayIndex is less than the total array size of the shader input.
        //! Emits an assert and returns false on failure; returns true on success. If validation is disabled true is always returned.
        bool ValidateBufferIndexAccess(ShaderInputIndex inputIndex, uint32_t arrayIndex) const;
        bool ValidateImageIndexAccess(ShaderInputIndex inputIndex, uint32_t arrayIndex) const;
        bool ValidateSamplerIndexAccess(ShaderInputIndex inputIndex, uint32_t arrayIndex) const;

        //! Validates that the inputIndex is valid.
        //! Emits an assert and returns false on failure; returns true on success. If validation is disabled true is always returned.
        bool ValidateBufferUnboundedArrayAccess(ShaderInputIndex inputIndex) const;
        bool ValidateImageUnboundedArrayAccess(ShaderInputIndex inputIndex) const;
        
    private:
        bool ValidateAccess(ShaderInputIndex inputIndex, size_t inputIndexLimit, const char* inputArrayTypeName) const;

        /// Helper functions for building up data caches for a single group of shader inputs.
        template<typename ShaderInputDescriptorType>
        bool FinalizeShaderInputGroup(
            const eastl::vector<ShaderInputDescriptorType>& shaderInputDescriptors,
            eastl::vector<Interval>& intervals,
            eastl::unordered_map<ShaderInputName, ShaderInputIndex>& nameMap,
            uint32_t& size);

        template<typename ShaderInputDescriptorType>
        bool FinalizeUnboundedArrayShaderInputGroup(
            const eastl::vector<ShaderInputDescriptorType>& shaderInputDescriptors,
            eastl::unordered_map<ShaderInputName, ShaderInputIndex>& nameMap,
            uint32_t& size);

        eastl::vector<ShaderInputStaticSamplerDescriptor> m_staticSamplers;
        eastl::vector<ShaderInputBufferDescriptor>        m_inputsForBuffers;
        eastl::vector<ShaderInputImageDescriptor>         m_inputsForImages;
        eastl::vector<ShaderInputSamplerDescriptor>       m_inputsForSamplers;

        eastl::vector<ShaderInputBufferUnboundedArrayDescriptor> m_inputsForBufferUnboundedArrays;
        eastl::vector<ShaderInputImageUnboundedArrayDescriptor>  m_inputsForImageUnboundedArrays;

        eastl::vector<Interval> m_intervalsForBuffers;
        eastl::vector<Interval> m_intervalsForImages;
        eastl::vector<Interval> m_intervalsForSamplers;

        eastl::unordered_map<ShaderInputName, ShaderInputIndex> m_buffersNameMap;
        eastl::unordered_map<ShaderInputName, ShaderInputIndex> m_imagesNameMap;
        eastl::unordered_map<ShaderInputName, ShaderInputIndex> m_samplersNameMap;

        eastl::unordered_map<ShaderInputName, ShaderInputIndex> m_unboundedBufferArrayMap;
        eastl::unordered_map<ShaderInputName, ShaderInputIndex> m_unboundedImageArrayMap;

        uint32_t m_bufferSize = 0;
        uint32_t m_imageSize = 0;
        uint32_t m_bufferUnboundedArraySize = 0;
        uint32_t m_imageUnboundedArraySize = 0;
        uint32_t m_samplerSize = 0;

        /// The logical binding slot used by all groups in this layout.
        uint32_t m_bindingSlot;

        /// The layout of the constants data.
        eastl::unique_ptr<ConstantsLayout> m_constantsDataLayout;

        /// The computed hash value.
        size_t m_hash  = 0;

    };
}
