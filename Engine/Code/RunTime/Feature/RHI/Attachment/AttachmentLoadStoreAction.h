/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/ClearValue.h>
#include "AttachmentEnums.h"

namespace Spark::RHI
{
    //! Describes what rules to apply when the image or buffer attachment is loaded and stored
    struct AttachmentLoadStoreAction
    {
        explicit AttachmentLoadStoreAction(
            const ClearValue& clearValue = ClearValue(),
            AttachmentLoadAction loadAction = AttachmentLoadAction::Load,
            AttachmentStoreAction storeAction = AttachmentStoreAction::Store,
            AttachmentLoadAction loadActionStencil = AttachmentLoadAction::None,
            AttachmentStoreAction storeActionStencil = AttachmentStoreAction::None);

        bool operator==(const AttachmentLoadStoreAction& other) const;
            
        /// The clear value if using a Clear load action. Ignored otherwise.
        ClearValue m_clearValue;

        /// The load action applied when the attachment is bound.
        AttachmentLoadAction m_loadAction = AttachmentLoadAction::Load;

        /// The store action applied when the attachment is bound.
        AttachmentStoreAction m_storeAction = AttachmentStoreAction::Store;

        /// The stencil load action. Applies only to depth-stencil image attachments.
        /// Defaults to None (stencil untouched): most passes don't use stencil, so the
        /// safe default is "not accessed". A pass that reads/writes stencil must set this
        /// (and m_storeActionStencil) explicitly, and the format must have a stencil plane.
        AttachmentLoadAction m_loadActionStencil = AttachmentLoadAction::None;

        /// The stencil store action. Applies only to depth-stencil image attachments.
        /// Defaults to None — see m_loadActionStencil.
        AttachmentStoreAction m_storeActionStencil = AttachmentStoreAction::None;
    };
}