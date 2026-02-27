/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>
#include <RHI/Resource/Buffer/IndirectBufferSignature.h>
#include <RHI/Resource/Buffer/IndirectBufferView.h>
#include <RHI/Resource/Buffer/StreamBufferView.h>
#include <RHI/Resource/Buffer/VertexBufferView.h>
#include "DrawArguments.h"
#include "IndirectArguments.h"

namespace Spark::RHI
{
    class PipelineState;
    class ShaderResource;
    struct Scissor;
    struct Viewport;

    // A DrawItem corresponds to one draw of one mesh in one pass. Multiple draw items are bundled
    // in a DrawPacket, which corresponds to multiple draws of one mesh in multiple passes.
    // NOTE: Do not rely solely on default member initialization here, as DrawItems are bulk allocated for
    // DrawPackets and their memory aliased in DrawPacketBuilder. Any default values should also be specified
    // in the DrawPacketBuilder::End() function (see DrawPacketBuilder.cpp)
    struct DrawItem
    {
        DrawItem() = default;

        DrawInstanceArguments m_drawInstanceArgs;

        /// Indices of the StreamBufferViews in the GeometryView that this DrawItem will use
        // StreamBufferIndices m_streamIndices;
        uint8_t m_stencilRef = 0;
        uint8_t m_shaderResourcCount = 0;
        uint8_t m_rootConstantSize = 0;
        uint8_t m_scissorsCount = 0;
        uint8_t m_viewportsCount = 0;

        // Remove?
        union
        {
            struct
            {
                // NOTE: If you add or update any of these flags, please update the default value of m_allFlags
                // so that your added flags are initialized properly. Also update the default value specified in
                // the DrawPacketBuilder::End() function (see DrawPacketBuilder.cpp). See comment above.

                bool m_enabled : 1;     // Whether the Draw Item should render
            };
            uint8_t m_allFlags = 1;     //< Update default value if you add flags. Also update in DrawPacketBuilder::End()
        };

        // --- Geometry ---
        DrawArguments m_drawArguments;

        IndexBufferView m_indexBufferView;

        VertexBufferView m_vertexBufferView;

        // --- Shader ---
        const PipelineState* m_pipelineState = nullptr;

        /// Array of shader resource groups to bind (count must match m_shaderResourceGroupCount).
        const ShaderResource* const* m_shaderResource = nullptr;

        /// Unique SRG, not shared within the draw packet. This is usually a per-draw SRG, populated with the shader variant fallback key
        const ShaderResource* m_uniqueShaderResource = nullptr;

        /// Array of root constants to bind (count must match m_rootConstantSize).
        const uint8_t* m_rootConstants = nullptr;

        // --- Scissor and Viewport ---
        /// List of scissors to be applied to this draw item only. Scissor will be restore to the previous state
        /// after the DeviceDrawItem has been processed.
        const Scissor* m_scissors = nullptr;

        /// List of viewports to be applied to this draw item only. Viewports will be restore to the previous state
        /// after the DeviceDrawItem has been processed.
        const Viewport* m_viewports = nullptr;
    };
}