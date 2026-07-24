/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "Base.h"

namespace Spark::RHI
{
    enum class ValidationMode
    {
        Disabled,
        // Print warnings and errors
        Enabled,
        // Print all warnings, errors and info messages
        Verbose,
        // Enable GPU-based validation
        GPU
    };

    // Validation carries a real per-frame CPU cost (D3D12 debug layer validates every
    // draw / barrier / resource op; Verbose also flushes all info messages each frame),
    // so it must track the build config instead of being a hardcoded constant. Release
    // ships with it off; Debug uses Enabled (not Verbose) to keep the info-spam cost down.
    // For a clean perf A/B, temporarily force this to Disabled and rebuild.
#if defined(NDEBUG)
    static const ValidationMode curValidationMode = ValidationMode::Disabled;
#else
    static const ValidationMode curValidationMode = ValidationMode::Enabled;
#endif
}
