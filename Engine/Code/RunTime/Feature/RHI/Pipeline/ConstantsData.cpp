/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Only the operation of the original Buffer is retained, and the template specialization for various data is removed
 */

#include "ConstantsData.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    ConstantsData::ConstantsData(const ConstantsLayout* layout) : m_layout(layout)
    {
        ASSERT(m_layout, "Null ConstantsLayout");
        if (m_layout->GetDataSize() > 0)
        {
            m_constantData.resize(m_layout->GetDataSize());
        }
    }

    bool ConstantsData::ValidateConstantAccess(ShaderInputIndex inputIndex, size_t offsetInBytes, size_t sizeInBytes) const
    {
        if (!Validation::isEnabled)
        {
            return true;
        }

        if (!GetLayout()->ValidateAccess(inputIndex))
        {
            return false;
        }

        const ShaderInputConstantDescriptor& shaderInputConstant = GetLayout()->GetShaderInput(inputIndex);
        if ((offsetInBytes + sizeInBytes) > shaderInputConstant.m_constantByteCount)
        {
            ASSERT(false,
                "Constant Input '{}': The requested region of constant data exceeds the allocated size of the constant shader input. "
                "Total Bytes: {} Actual: [{}, {}).", shaderInputConstant.m_name.GetCStr(), shaderInputConstant.m_constantByteCount, offsetInBytes, offsetInBytes + sizeInBytes);
            return false;
        }
        return true;
    }

    bool ConstantsData::ValidateConstantBufferAccess(size_t offsetInBytes, size_t sizeInBytes) const
    {
        if (Validation::isEnabled)
        {
            if ((offsetInBytes + sizeInBytes) > m_constantData.size())
            {
                LOG_ERROR("[ConstantsData] Exceeded size declared for constant buffer.");
                return false;
            }
        }
        return true;
    }

    bool ConstantsData::SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, size_t byteCount)
    {
        return SetConstantRaw(inputIndex, bytes, 0, byteCount);
    }

    bool ConstantsData::SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, size_t byteOffset, size_t byteCount)
    {
        if (ValidateConstantAccess(inputIndex, byteOffset, byteCount))
        {
            const Interval interval = GetLayout()->GetInterval(inputIndex);
            memcpy(&m_constantData[interval.m_min + byteOffset], bytes, byteCount);
            return true;
        }
        return false;
    }

    bool ConstantsData::SetConstantData(const void* bytes, size_t byteCount)
    {
        if (ValidateConstantBufferAccess(0, byteCount))
        {
            memcpy(m_constantData.data(), bytes, byteCount);
            return true;
        }
        return false;
    }

    bool ConstantsData::SetConstantData(const void* bytes, size_t byteOffset, size_t byteCount)
    {
        if (ValidateConstantBufferAccess(byteOffset, byteCount))
        {
            memcpy(&m_constantData[byteOffset], bytes, byteCount);
            return true;
        }
        return false;
    }

    eastl::span<const uint8_t> ConstantsData::GetConstantRaw(ShaderInputIndex inputIndex) const
    {
        const Interval interval = GetLayout()->GetInterval(inputIndex);
        return eastl::span<const uint8_t>(&m_constantData[interval.m_min], interval.m_max - interval.m_min);
    }

    eastl::span<const uint8_t> ConstantsData::GetConstantData() const
    {
        return m_constantData;
    }

    const ConstantsLayout* ConstantsData::GetLayout() const
    {
        ASSERT(m_layout, "Constants layout is null");
        return m_layout.get();
    }
}