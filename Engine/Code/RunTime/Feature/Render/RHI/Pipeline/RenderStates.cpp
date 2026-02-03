/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "RenderStates.h"

#include <EASTLEX/hash.h>

namespace Spark::RHI
{
    bool RenderStates::operator == (const RenderStates& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(RenderStates)) == 0);
    }

    bool RasterState::operator == (const RasterState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(RasterState)) == 0);
    }

    bool StencilOpState::operator == (const StencilOpState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(StencilOpState)) == 0);
    }

    bool DepthState::operator == (const DepthState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(DepthState)) == 0);
    }

    bool StencilState::operator == (const StencilState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(StencilState)) == 0);
    }

    bool DepthStencilState::operator == (const DepthStencilState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(DepthStencilState)) == 0);
    }

    bool TargetBlendState::operator == (const TargetBlendState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(TargetBlendState)) == 0);
    }

    bool BlendState::operator == (const BlendState& rhs) const
    {
        return (memcmp(this, &rhs, sizeof(BlendState)) == 0);
    }

    size_t RenderStates::GetHash(size_t seed) const
    {
        eastl::hash_combine(seed, this);
        return seed;
    }
}