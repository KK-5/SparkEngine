/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Sampler.h"

#include <D3D12Factory.h>
#include <Device/Device.h>
#include <Descriptor/DescriptorContext.h>

namespace Spark::RHI::DX12
{
    void Sampler::Init(Device& device, const RHI::SamplerState& samplerState)
    {
        Base::Init(device);
        DescriptorContext& context = Service<D3D12FactoryInterface>::Get()->AcquireDescriptorContext();;
        context.CreateSampler(samplerState, m_descriptor);
    }

    DescriptorHandle Sampler::GetDescriptorHandle() const
    {
        return m_descriptor;
    }

    void Sampler::Shutdown()
    {
        auto& device = static_cast<Device&>(GetDevice());
        DescriptorContext& context = Service<D3D12FactoryInterface>::Get()->AcquireDescriptorContext();;
        context.ReleaseDescriptor(m_descriptor);
        m_descriptor = DescriptorHandle();
        Base::Shutdown();
    }
}