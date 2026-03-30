/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2026
 *  -- RHI clear / discard requests shared by CommandList backends.
 */
#pragma once

#include <Math/Bit.h>
#include <RHI/ClearValue.h>

namespace Spark::RHI
{
    class ImageView;
    class BufferView;

    //! Which aspects to clear when using ClearRenderTarget with a depth-stencil image view.
    enum class DepthStencilClearFlags : uint32_t
    {
        None = 0,
        Depth = BIT(0),
        Stencil = BIT(1),
        DepthStencil = Depth | Stencil
    };
    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::DepthStencilClearFlags, uint32_t);

    struct ImageClearRequest
    {
        ClearValue m_clearValue{};
        DepthStencilClearFlags m_depthStencilClearFlags = DepthStencilClearFlags::DepthStencil;
        const ImageView* m_imageView = nullptr;
    };

    struct BufferClearRequest
    {
        ClearValue m_clearValue{};
        const BufferView* m_bufferView = nullptr;
    };
}
