/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/intrusive_ptr.h>

namespace Spark
{
    template <typename T>
    using Ptr = eastl::intrusive_ptr<T>;

    template <typename T>
    using ConstPtr = eastl::intrusive_ptr<const T>;
}