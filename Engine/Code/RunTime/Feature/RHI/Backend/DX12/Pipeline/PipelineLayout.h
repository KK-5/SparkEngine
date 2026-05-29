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

#include <EASTL/array.h>
#include <EASTL/fixed_vector.h>

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Device/DeviceObject.h>
#include <DX12.h>
#include "PipelineLayoutDescriptor.h"


namespace Spark::RHI::DX12
{
    class Device;

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

        /// Returns the number of root parameter bindings (1-to-1 with SRG).
        size_t GetRootParameterBindingCount() const;

        /// Returns the root parameter binding for the flat index.
        RootParameterBinding GetRootParameterBindingByIndex(size_t index) const;

        /// Returns the root parameter index for the Root Constants.
        RootParameterIndex GetRootConstantsRootParameterIndex() const;

        /// Returns the SRG binding slot associated with the SRG flat index.
        size_t GetSlotByIndex(size_t index) const;

        /// Returns the SRG flat index associated with the SRG binding slot.
        size_t GetIndexBySlot(size_t slot) const;

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

    private:
        void BuildRootCanstants(const PipelineLayoutDescriptor* desc, eastl::vector<D3D12_ROOT_PARAMETER>& parameters);

        void BuildShaderResourceConstants(
            const PipelineLayoutDescriptor* desc,
            const eastl::vector<uint8_t>& sortedIndex,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters
        );

        void BuildShaderResourceBuffersAndImages(
            const PipelineLayoutDescriptor* desc,
            const eastl::vector<uint8_t>& sortedIndex,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
            eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[]
        );

        void BuildShaderResourceSamplers(
            const PipelineLayoutDescriptor* desc,
            const eastl::vector<uint8_t>& sortedIndex,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
            eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[]
        );

        void BuildStaticSamplers(
            const PipelineLayoutDescriptor* desc, 
            const eastl::vector<uint8_t>& sortedIndex,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters, 
            eastl::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers
        );

        /// New path
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

        /// Tables for mapping between SRG slots (sparse) to SRG indices (packed).
        eastl::array<uint8_t, RHI::Limits::Pipeline::ShaderResourceCountMax> m_slotToIndexTable;
        eastl::fixed_vector<uint8_t, RHI::Limits::Pipeline::ShaderResourceCountMax> m_indexToSlotTable;

        /// Table for mapping SRG index (packed) to Root Parameter Binding (DX12 command list bindings).
        eastl::fixed_vector<RootParameterBinding, RHI::Limits::Pipeline::ShaderResourceCountMax> m_indexToRootParameterBindingTable;

        /// Root Parameter Index for root constants.
        RootParameterIndex m_rootConstantsRootParameterIndex;

        /// Tracks whether this pipeline layout has inline constants.
        bool m_hasRootConstants = false;

        /// New path
        eastl::fixed_vector<RootParameterBinding, RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_spaceRootParams;

        Ptr<ID3D12RootSignature> m_signature;
        ConstPtr<RHI::PipelineLayoutDescriptor> m_layoutDescriptor;
        size_t m_hash{ 0 };
        ID3D12DeviceX* m_d3d12Device;
    };
}