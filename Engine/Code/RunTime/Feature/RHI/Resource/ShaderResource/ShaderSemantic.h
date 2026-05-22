/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

namespace Spark::RHI
{
    // Holds the HLSL/SPIR-V vertex input semantic name ("POSITION", "TEXCOORD", ...) plus
    // its array index. The name is forwarded verbatim to D3D12_INPUT_ELEMENT_DESC::SemanticName,
    // so it must remain a live string in release builds — cannot be hash-stripped via ObjectName.
    class ShaderSemantic
    {
    public:
        static constexpr const char UvStreamSemantic[] = "UV";

        ShaderSemantic() = default;
        explicit ShaderSemantic(eastl::string_view name, size_t index = 0);

        bool operator==(const ShaderSemantic& rhs) const;

        size_t GetHash() const;

        eastl::string ToString() const;

        eastl::string m_name;
        uint32_t      m_index = 0;
    };
}