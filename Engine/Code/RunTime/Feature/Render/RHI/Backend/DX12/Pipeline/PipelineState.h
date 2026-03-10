/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/Pipeline/PipelineState.h>
#include "PipelineLayout.h"

namespace Spark::RHI::DX12
{
    struct PipelineStateDrawData
    {
        RHI::MultisampleState m_multisampleState;
        RHI::PrimitiveTopology m_primitiveTopology = RHI::PrimitiveTopology::Undefined;
    };

    struct PipelineStateData
    {
        PipelineStateData()
            : m_type(RHI::PipelineStateType::Draw)
        {}

        RHI::PipelineStateType m_type;
        union
        {
            // Only draw data for now.
            PipelineStateDrawData m_drawData;
        };
    };

    class PipelineState final : public RHI::PipelineState
    {
    public:
        /// Returns the pipeline layout associated with this PSO.
        const PipelineLayout* GetPipelineLayout() const;

        /// Returns the platform pipeline state object.
        ID3D12PipelineState* Get() const;

        const PipelineStateData& GetPipelineStateData() const;

    private:
        PipelineState() = default;

        friend class DeviceObjectFactory<PipelineState>;

        //////////////////////////////////////////////////////////////////////////
        // RHI::DevicePipelineState
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::PipelineStateDescriptorForDraw& descriptor, RHI::PipelineLibrary* pipelineLibrary) override;
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::PipelineStateDescriptorForDispatch& descriptor, RHI::PipelineLibrary* pipelineLibrary) override;
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::PipelineStateDescriptorForRayTracing& descriptor, RHI::PipelineLibrary* pipelineLibrary) override;
        void ShutdownInternal() override;
        //////////////////////////////////////////////////////////////////////////

        ConstPtr<PipelineLayout> m_pipelineLayout;
        Ptr<ID3D12PipelineState> m_pipelineState;
        PipelineStateData m_pipelineStateData;
    };
}