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
#pragma once

#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/span.h>
#include <RHI/Base.h>

#include "ConstantsLayout.h"

namespace Spark::RHI
{

    //! The intent of this class is to provide fast and thin access to the underlying constant
    //! data (inline or from an SRG), with basic validation to protect the user. As a secondary objective, it provides type-specific convenience
    //! operations as long as they don't violate the primary "fast" and "thin" objectives. To clarify, thin means
    //! we don't make assumptions about the data or how the user wants to operate on the data, and the convenience
    //! operations boil down to thin wrappers for single calls to SetConstantRaw and GetConstantRaw. So these
    //! convenience functions are provided in situations that are "low-hanging-fruit".
    class ConstantsData final
    {
    public:
        ConstantsData() = default;
        explicit ConstantsData(const ConstantsLayout* layout);

        //! Assigns constant data for the given constant shader input index.
        bool SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, size_t byteCount);
        bool SetConstantRaw(ShaderInputIndex inputIndex, const void* bytes, size_t byteOffset, size_t byteCount);

        //! Assigns constant data as a whole.
        bool SetConstantData(const void* bytes, size_t byteCount);
        bool SetConstantData(const void* bytes, size_t byteOffset, size_t byteCount);

        //! Returns constant data for the given shader input index as a span of bytes.
        eastl::span<const uint8_t> GetConstantRaw(ShaderInputIndex inputIndex) const;

        //! Returns the opaque constant data populated by calls to SetConstant and SetConstantData.
        eastl::span<const uint8_t> GetConstantData() const;

        //! Returns the constants layout.
        const ConstantsLayout* GetLayout() const;

    private:
        bool ValidateConstantAccess(ShaderInputIndex inputIndex, size_t offsetInBytes, size_t sizeInBytes) const;
        bool ValidateConstantBufferAccess(size_t offsetInBytes, size_t sizeInBytes) const;

        eastl::unique_ptr<const ConstantsLayout> m_layout;
        eastl::vector<uint8_t> m_constantData;
    };
}