/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <RHI/Resource/ResourcePoolDescriptor.h>
#include "ShaderResourceLayout.h"

namespace Spark::RHI
{
    enum class ShaderResourceGroupUsage : uint32_t
    {
        // The resource set is persistent across frames and will be updated rarely.
        Persistent = 0,

        // The resource group is optimized for once-per-frame updates. Its contents become invalid
        // after the current frame has completed.
        Transient
    };

    class ShaderResourcePoolDescriptor
        : public ResourcePoolDescriptor
    {
    public:
        virtual ~ShaderResourcePoolDescriptor() = default;
        ShaderResourcePoolDescriptor() = default;

        /// [Not Serialized] The resource group layout shared by all resource groups in the pool.
        ConstPtr<ShaderResourceLayout> m_layout;

        /// The usage type used to update resource groups.
        ShaderResourceGroupUsage m_usage = ShaderResourceGroupUsage::Persistent;
    };
}