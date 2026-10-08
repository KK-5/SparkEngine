/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandList.h"

#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Image/Image.h>

namespace Spark::RHI
{
    const ShadingRateCombinators CommandList::DefaultShadingRateCombinators = { { ShadingRateCombinerOp::Passthrough,
                                                                                  ShadingRateCombinerOp::Passthrough } };

    void CommandList::SetResourceState(Buffer& buffer, ResourceState state)
    {
        buffer.SetResourceState(state);
    }

    void CommandList::SetResourceState(Image& image, const ImageSubresourceRange& range, ResourceState state)
    {
        image.SetResourceState(range, state);
    }
}