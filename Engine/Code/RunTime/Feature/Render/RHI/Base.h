/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */    
#pragma once

#include <Math/Bit.h>

namespace Spark::Render::RHI
{

    /**
    * A set of general result codes used by methods which may fail.
    */
    enum class ResultCode : uint32_t
    {
        // The operation succeeded.
        Success = 0,

        // The operation failed with an unknown error.
        Fail,

        // The operation failed due being out of memory.
        OutOfMemory,

        // The operation failed because the feature is unimplemented on the particular platform.
        Unimplemented,

        // The operation failed because the API object is not in a state to accept the call.
        InvalidOperation,

        // The operation failed due to invalid arguments.
        InvalidArgument,

        // The operation is not ready
        NotReady
    };

    enum class APIIndex : uint32_t
    {
        Null = 0,
        DX12,
        Vulkan,
    };

    enum class DrawListSortType : uint8_t
    {
        KeyThenDepth = 0,
        KeyThenReverseDepth,
        DepthThenKey,
        ReverseDepthThenKey
    };
        
    enum class Scaling : uint32_t
    {
        None = 0,               // No scaling
        Stretch,                // Scale the source to fit the target
        AspectRatioStretch,     // Scale the source to fit the target while preserving the aspect ratio of the source
    };

    //! Flags for specifying supported Scaling modes
    enum class ScalingFlags : uint32_t
    {
        None = 0,
        Stretch = BIT(static_cast<uint32_t>(Scaling ::Stretch)),
        AspectRatioStretch = BIT(static_cast<uint32_t>(Scaling ::AspectRatioStretch)),
        All = Stretch | AspectRatioStretch
    };
}