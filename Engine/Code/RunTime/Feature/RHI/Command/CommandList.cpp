/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandList.h"

namespace Spark::RHI
{
    const ShadingRateCombinators CommandList::DefaultShadingRateCombinators = { { ShadingRateCombinerOp::Passthrough,
                                                                                  ShadingRateCombinerOp::Passthrough } };
}