/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2026
 *  -- Stripped per-pass data (PSO, PerPass SRGs — auto-bound by executer).
 *  -- Replaced bulk-allocated raw pointers with inline fixed_vector storage
 *     for per-draw SRGs, viewports, scissors, and root constants.
 *  -- Usable as an ECS component on DrawItem entities.
 */
#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>
#include <RHI/Resource/Buffer/VertexBufferView.h>
#include <RHI/Scissor/Scissor.h>
#include <RHI/Viewport/Viewport.h>
#include "DrawArguments.h"

namespace Spark::RHI
{
    class ShaderResource;

    // Per-draw data: geometry, per-draw SRGs, optional viewport/scissor overrides,
    // and future root constants. PSO and PerPass SRGs are not here — the executer
    // auto-binds those at pass begin.
    struct DrawItem
    {
        DrawItem() = default;

        DrawArguments         m_drawArguments;
        DrawInstanceArguments m_drawInstanceArgs;
        uint8_t               m_stencilRef = 0;
        bool                  m_enabled = true;

        // Geometry
        IndexBufferView  m_indexBufferView;
        VertexBufferView m_vertexBufferView;

        // Per-draw SRGs (material + unique). Populated in Build, bound by lambda
        // or Submit() before the draw call.
        eastl::fixed_vector<Ptr<ShaderResource>, Limits::Pipeline::ShaderResourceCountMax> m_shaderResources;

        // Per-draw viewport / scissor overrides (rare, e.g. multi-viewport passes).
        uint8_t m_scissorsCount = 0;
        uint8_t m_viewportsCount = 0;
        eastl::fixed_vector<Viewport, Limits::Pipeline::AttachmentColorCountMax> m_viewports;
        eastl::fixed_vector<Scissor,  Limits::Pipeline::AttachmentColorCountMax> m_scissors;

        // Root constants (future support).
        uint8_t m_rootConstantSize = 0;
        eastl::fixed_vector<uint8_t, Limits::Pipeline::RootConstantByteCountMax> m_rootConstants;
    };
}
