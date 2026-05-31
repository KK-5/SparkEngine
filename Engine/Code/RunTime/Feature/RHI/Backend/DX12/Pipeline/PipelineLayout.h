/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove the PipelineLayoutCache. ID3D12Factory will manager the PipelineLayout object.
 */

#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Device/DeviceObject.h>
#include <DX12.h>
#include "PipelineLayoutDescriptor.h"


namespace Spark::RHI::DX12
{
    class Device;

    /**
     * Root parameter indices for the K root CBVs of one SpaceGroup. The k-th entry
     * here corresponds 1:1 with RHI::ShaderInputGroup::m_constantBuffers[k] — same
     * register, same iteration order. Byte layout itself lives on the RHI-layer
     * ConstantBufferLayout; this struct only carries the DX12-specific binding info.
     */
    struct SpaceCBVBinding
    {
        static constexpr uint32_t MaxCount = 8;
        eastl::fixed_vector<RootParameterIndex, MaxCount> m_rootIndices;
    };

    /**
     * Root parameter indices for the descriptor tables of a SpaceGroup.
     */
    struct SpaceTableBinding
    {
        RootParameterIndex m_resourceTable = InvalidRootParameterIndex;
        RootParameterIndex m_samplerTable  = InvalidRootParameterIndex;
    };

    /**
     * PipelineLayouts are created from a cache. They are internally de-duplicated using the hash value computed
     * by the descriptor. Ownership of a particular element in the cache is still externally managed (via ConstPtr).
     * When all references to a particular instance are destroyed, the object is unregistered from the cache.
     * Therefore, ownership is still managed by the application and the cache doesn't grow unbounded.
     */
    class PipelineLayout final : public RHI::DeviceObject
    {
        // friend class PipelineLayoutCache;

    public:
        PipelineLayout() = default;
        ~PipelineLayout() = default;

        /// Initializes the pipeline layout.
        void Init(Device& device, const RHI::PipelineLayoutDescriptor& descriptor);

        void Shutdown() override;

        /// Returns the root parameter index for the Root Constants.
        RootParameterIndex GetRootConstantsRootParameterIndex() const;

        /// Returns whether this pipeline layout has inline constants.
        bool HasRootConstants() const;

        const RHI::PipelineLayoutDescriptor& GetPipelineLayoutDescriptor() const;

        /// Returns the platform pipeline layout object.
        ID3D12RootSignature* Get() const;

        /// Returns the hash of the pipeline layout provided by the descriptor.
        size_t GetHash() const;

        // New path
        const RootParameterBinding& GetSpaceBinding(uint32_t spaceIndex) const;

        size_t GetSpaceGroupCount() const;

        const SpaceCBVBinding&   GetSpaceCBVBinding(uint32_t spaceIndex) const;
        const SpaceTableBinding& GetSpaceTableBinding(uint32_t spaceIndex) const;

        /// Resolve an HLSL space id to the parallel array index used by
        /// GetSpaceCBVBinding / GetSpaceTableBinding. Returns -1 if not present.
        int32_t FindSpaceIndexBySpaceId(uint32_t spaceId) const;

    private:
        void BuildRootCanstants(const PipelineLayoutDescriptor* desc, eastl::vector<D3D12_ROOT_PARAMETER>& parameters);

        void BuildSpaceGroupConstants(
            const PipelineLayoutDescriptor* desc,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters
        );

        void BuildSpaceGroupResources(
            const PipelineLayoutDescriptor* desc,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
            eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[]
        );

        void BuildSpaceGroupSamplers(
            const PipelineLayoutDescriptor* desc,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
            eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[]
        );

        void BuildSpaceGroupStaticSamplers(
            const PipelineLayoutDescriptor* desc,
            eastl::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers
        );

        /// Root Parameter Index for root constants.
        RootParameterIndex m_rootConstantsRootParameterIndex;

        /// Tracks whether this pipeline layout has inline constants.
        bool m_hasRootConstants = false;

        eastl::fixed_vector<RootParameterBinding, RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_spaceRootParams;
        eastl::fixed_vector<SpaceCBVBinding,       RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_spaceCBVBindings;
        eastl::fixed_vector<SpaceTableBinding,     RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_spaceTableBindings;

        Ptr<ID3D12RootSignature> m_signature;
        ConstPtr<RHI::PipelineLayoutDescriptor> m_layoutDescriptor;
        size_t m_hash{ 0 };
        ID3D12DeviceX* m_d3d12Device;
    };
}