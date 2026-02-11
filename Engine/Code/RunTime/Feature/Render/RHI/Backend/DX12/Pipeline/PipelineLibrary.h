/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- ID3D12PipelineLibrary is not currently in use.
 */

#pragma once

#include <RHI/Pipeline/PipelineLibrary.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    class PipelineLibrary final : RHI::PipelineLibrary
    {
    public:
        Ptr<ID3D12PipelineState> CreateGraphicsPipelineState(uint64_t hash, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineStateDesc);
        Ptr<ID3D12PipelineState> CreateComputePipelineState(uint64_t hash, const D3D12_COMPUTE_PIPELINE_STATE_DESC& pipelineStateDesc);

    private:
        PipelineLibrary() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::PipelineLibrary
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::PipelineLibraryDescriptor& descriptor) override;
        void ShutdownInternal() override;
        // RHI::ResultCode MergeIntoInternal(eastl::span<const RHI::PipelineLibrary* const> libraries) override;
        // ConstPtr<RHI::PipelineLibraryData> GetSerializedDataInternal() const override;
        // bool IsMergeRequired() const;
        // bool SaveSerializedDataInternal(const AZStd::string& filePath) const override;
        //////////////////////////////////////////////////////////////////////////

        ID3D12DeviceX* m_d3d12Device = nullptr;
    };
}