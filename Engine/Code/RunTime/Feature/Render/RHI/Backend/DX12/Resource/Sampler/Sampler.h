/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Device/DeviceObject.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <Descriptor/Descriptor.h>

namespace Spark::RHI::DX12
{
    class Device;
    
    class Sampler final : public RHI::DeviceObject
    {
        using Base = RHI::DeviceObject;
    public:
        ~Sampler() = default;

        void Init(Device& device, const RHI::SamplerState& samplerState);

        /// Returns the descriptor handle
        DescriptorHandle GetDescriptorHandle() const;
    private:
        Sampler() = default;

        friend class DeviceObjectFactory<Sampler>;

        void Shutdown() override;

        DescriptorHandle m_descriptor;
    };
}