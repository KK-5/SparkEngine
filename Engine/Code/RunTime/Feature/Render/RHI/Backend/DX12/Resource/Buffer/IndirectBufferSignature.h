/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/vector.h>

#include <RHI/Resource/Buffer/IndirectBufferSignature.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    //! DX12 implementation of the RHI IndirectBufferSignature. 
    //! It represents the DX12 object ID3D12CommandSignature when doing indirect rendering. 
    class IndirectBufferSignature final : public RHI::IndirectBufferSignature
    {
    public:
        ID3D12CommandSignature* Get() const;

    private:
        IndirectBufferSignature() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::IndirectBufferSignature
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::IndirectBufferSignatureDescriptor& descriptor) override;
        uint32_t GetByteStrideInternal() const override;
        uint32_t GetOffsetInternal(RHI::IndirectCommandIndex index) const override;
        void ShutdownInternal() override;
        //////////////////////////////////////////////////////////////////////////

        Ptr<ID3D12CommandSignature> m_signature;

        uint32_t m_stride = 0;
        eastl::vector<uint32_t> m_offsets;
    };
}