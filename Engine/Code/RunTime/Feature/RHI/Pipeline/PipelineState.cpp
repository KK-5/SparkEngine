/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineState.h"

#include <Log/SpdLogSystem.h>

#include "PipelineLibrary.h"

namespace Spark::RHI
{
    bool PipelineState::ValidateNotInitialized() const
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[PipelineState] PipelineState already initialized!");
                return false;
            }
        }

        return true;
    }

    ResultCode PipelineState::Init(Device& device, const PipelineStateDescriptor& descriptor, PipelineLibrary* pipelineLibrary)
    {
        ResultCode resultCode = ResultCode::Success;

        switch (descriptor.GetType())
        {
            case PipelineStateType::Draw:
            {
                resultCode = Init(device, static_cast<const PipelineStateDescriptorForDraw&>(descriptor), pipelineLibrary);
                break;
            }
            case PipelineStateType::Dispatch:
            {
                resultCode = Init(device, static_cast<const PipelineStateDescriptorForDispatch&>(descriptor), pipelineLibrary);
                break;
            }
            case PipelineStateType::RayTracing:
            {
                resultCode = Init(device, static_cast<const PipelineStateDescriptorForRayTracing&>(descriptor), pipelineLibrary);
                break;
            }
            default:
            {
                LOG_ERROR("[PipelineState] Unknown PipelineStateType!");
                resultCode = ResultCode::InvalidArgument;
                break;
            }
        }

        return resultCode;
    }

    ResultCode PipelineState::Init(Device& device, const PipelineStateDescriptorForDraw& descriptor, PipelineLibrary* pipelineLibrary)
    {
        if (IsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (Validation::isEnabled)
        {
            bool error = false;

            if (!descriptor.m_inputStreamLayout.IsFinalized())
            {
                LOG_ERROR("[PipelineState] InputStreamLayout is not finalized!");
                error = true;
            }

            const auto& renderTargetLayout = descriptor.m_renderTargetLayout;

            if (descriptor.m_renderStates.m_depthStencilState.m_depth.m_enable || descriptor.m_renderStates.m_depthStencilState.m_stencil.m_enable)
            {
                if (renderTargetLayout.m_depthStencilFormat == RHI::Format::Unknown)
                {
                    LOG_ERROR("[PipelineState] Depth-stencil format is not set.");
                    error = true;
                }
            }

            for (uint32_t i = 0; i < renderTargetLayout.m_colorAttachmentCount; ++i)
            {
                if (renderTargetLayout.m_colorFormats[i] == RHI::Format::Unknown)
                {
                    LOG_ERROR("[PipelineState] Rendertarget attachment {} format is not set.", i);
                    error = true;
                }
            }

            for (uint32_t i = 0; i < renderTargetLayout.m_colorAttachmentCount; ++i)
            {
                if (renderTargetLayout.m_resolveFormats[i] != RHI::Format::Unknown &&
                    renderTargetLayout.m_resolveFormats[i] != renderTargetLayout.m_colorFormats[i])
                {
                    LOG_ERROR("[PipelineState] Invalid resolve format for attachment {}.", i);
                    error = true;
                }
            }

            if (error)
            {
                return ResultCode::InvalidOperation;
            }
        }

        const ResultCode resultCode = InitInternal(device, descriptor, pipelineLibrary);

        if (resultCode == ResultCode::Success)
        {
            m_type = PipelineStateType::Draw;
            DeviceObject::Init(device);
        }

        return resultCode;
    }

    ResultCode PipelineState::Init(Device& device, const PipelineStateDescriptorForDispatch& descriptor, PipelineLibrary* pipelineLibrary)
    {
        if (!ValidateNotInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        const ResultCode resultCode = InitInternal(device, descriptor, pipelineLibrary);

        if (resultCode == ResultCode::Success)
        {
            m_type = PipelineStateType::Dispatch;
            DeviceObject::Init(device);
        }

        return resultCode;
    }

    ResultCode PipelineState::Init(Device& device, const PipelineStateDescriptorForRayTracing& descriptor, PipelineLibrary* pipelineLibrary)
    {
        if (!ValidateNotInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        const ResultCode resultCode = InitInternal(device, descriptor, pipelineLibrary);

        if (resultCode == ResultCode::Success)
        {
            m_type = PipelineStateType::RayTracing;
            DeviceObject::Init(device);
        }

        return resultCode;
    }

    void PipelineState::Shutdown()
    {
        if (IsInitialized())
        {
            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }

    PipelineStateType PipelineState::GetType() const
    {
        return m_type;
    }
}