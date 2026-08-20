/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2026
 *  -- Stripped everything the executer establishes per pass or per DrawList:
 *     PSO, SRGs, viewport, scissor.
 *  -- Usable as an ECS component on DrawItem entities.
 */
#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>
#include <RHI/Resource/Buffer/VertexBufferView.h>
#include "DrawArguments.h"

namespace Spark::RHI
{
    // Per-draw data: geometry and draw arguments. Everything else the submit path
    // reads is bound by the executer before the batch.
    struct DrawItem
    {
        DrawItem() = default;

        DrawArguments         m_drawArguments;
        DrawInstanceArguments m_drawInstanceArgs;
        uint8_t               m_stencilRef = 0;

        // Geometry
        IndexBufferView  m_indexBufferView;
        VertexBufferView m_vertexBufferView;

        // Root constants: no producer yet, kept for future per-draw constants. Size it
        // to the few DWORDs actually needed, not the 256-byte root signature budget.
        // uint8_t m_rootConstantSize = 0;
        // eastl::fixed_vector<uint8_t, Limits::Pipeline::RootConstantByteCountMax> m_rootConstants;
    };
}
