/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/array.h>
#include <EASTL/span.h>

#include <Base.h>
#include <Object/Object.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/ShaderResource/ConstantsLayout.h>
#include "ShaderStages.h"

namespace Spark::RHI
{
    struct ResourceBindingInfo
    {
        ResourceBindingInfo() = default;
        ResourceBindingInfo(const RHI::ShaderStageMask& mask, uint32_t registerId, uint32_t spaceId)
            : m_shaderStageMask{ mask }
            , m_registerId{ registerId }
            , m_spaceId{ spaceId }
        {}

        /// Returns the hash computed for the binding info.
        size_t GetHash() const;

        using Register = uint32_t;
        static const Register InvalidRegister = ~0u;

        /// Usage mask of resource.
        RHI::ShaderStageMask    m_shaderStageMask = RHI::ShaderStageMask::None;
        /// Register id of a resource.
        Register                m_registerId = InvalidRegister;
        /// Space id of the resource.
        uint32_t                m_spaceId = InvalidRegister;
    };

    //! This class describes binding information about the Shader Resource
    //! that is part of a Pipeline. Contains the register number for each SRG resource.
    struct ShaderResourceBindingInfo
    {
        ShaderResourceBindingInfo() = default;

        /// Returns the hash computed for the binding info.
        size_t GetHash() const;

        /// Register number for the constant data. All constants have the same register number.
        ResourceBindingInfo m_constantDataBindingInfo;
        /// Register number for the Shader Resource resources.
        eastl::unordered_map<RHI::InputName, ResourceBindingInfo> m_resourcesRegisterMap;
    };

    //! This class describes shader bindings to the RHI platform backend when creating a PipelineState.
    //! The base class contains a ShaderResourceLayout table ordered by frequency of update. The platform
    //! descriptor implementation augments this table with low-level shader binding information.
    //!
    //! In short, if the shader compiler needs to communicate platform-specific shader binding information
    //! when constructing a pipeline state, this is the place to do it. Platforms are expected to override
    //! this class in their PLATFORM.Reflect library, which is then exposed to the offline shader compiler.
    class PipelineLayoutDescriptor
        : public Object
    {
        friend class Factory;
    public:
        virtual ~PipelineLayoutDescriptor() noexcept = default;

        bool IsFinalized() const;

        //! Resets the descriptor back to an empty state.
        void Reset();

        //! Adds the layout info of shader resource, ordered by frequency of update.
        void AddShaderResourceLayoutInfo(const ShaderResourceLayout& layout, const ShaderResourceBindingInfo& shaderResourceInfo);

        //! Sets the layout of inline constants.
        void SetRootConstantsLayout(const ConstantsLayout& rootConstantsLayout);

        //! Finalizes the descriptor for use. Must be called prior to serialization. Should not be called
        //! after serialization.
        RHI::ResultCode Finalize();

        //! Returns the number of shader resource layouts added to this pipeline layout.
        size_t GetShaderResourceLayoutCount() const;

        //! Returns the shader resource layout pointer at the requested index.
        const ShaderResourceLayout* GetShaderResourceLayout(size_t index) const;

        //! Returns the shader resource binding info at the requested index.
        const ShaderResourceBindingInfo& GetShaderResourceBindingInfo(size_t index) const;

        //! Returns the inline constants layout.
        const ConstantsLayout* GetRootConstantsLayout() const;

        //! Returns the hash computed for the pipeline layout.
        size_t GetHash() const;

        //! Converts from an SRG binding slot to a shader resource index.
        uint32_t GetShaderResourceIndexFromBindingSlot(uint32_t bindingSlot) const;

    protected:
        PipelineLayoutDescriptor() = default;

        using ShaderResourceLayoutInfo = eastl::pair<Ptr<ShaderResourceLayout>, ShaderResourceBindingInfo>;

        const eastl::fixed_vector<ShaderResourceLayoutInfo, RHI::Limits::Pipeline::ShaderResourceCountMax>& GetShaderResourceLayoutInfo() const;

    private:
        ///////////////////////////////////////////////////////////////////
        // Platform API

        //! Called when the pipeline layout descriptor is being reset to an empty state.
        virtual void ResetInternal();

        //! Called when the pipeline layout descriptor is being finalized.
        virtual ResultCode FinalizeInternal();

        //! Computes the hash of the platform-dependent descriptor (combined with the provided seed value).
        virtual size_t GetHashInternal(size_t seed) const;

        ///////////////////////////////////////////////////////////////////

        // A hash of 0 is valid if the descriptor is empty.
        static constexpr size_t InvalidHash = static_cast<size_t>(~0);

        /// List of layout and binding information for each Shader Resource that is part of this Pipeline.
        eastl::fixed_vector<ShaderResourceLayoutInfo, RHI::Limits::Pipeline::ShaderResourceCountMax> m_shaderResourceLayoutsInfo;

        /// Layout info about inline constants.
        Ptr<ConstantsLayout> m_rootConstantsLayout;

        /// Mapping from a Shader Resource binding slot to the index into the m_shaderResourceLayoutsInfo.
        eastl::array<uint32_t, RHI::Limits::Pipeline::ShaderResourceCountMax> m_bindingSlotToIndex = {};

        size_t m_hash = InvalidHash;
    };
}