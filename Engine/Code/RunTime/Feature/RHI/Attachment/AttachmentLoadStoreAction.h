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
            AttachmentLoadAction loadActionStencil = AttachmentLoadAction::Load,
            AttachmentStoreAction storeActionStencil = AttachmentStoreAction::Store);

        bool operator==(const AttachmentLoadStoreAction& other) const;
            
        /// The clear value if using a Clear load action. Ignored otherwise.
        ClearValue m_clearValue;

        /// The load action applied when the attachment is bound.
        AttachmentLoadAction m_loadAction = AttachmentLoadAction::Load;

        /// The store action applied when the attachment is bound.
        AttachmentStoreAction m_storeAction = AttachmentStoreAction::Store;

        /// The stencil load action. Applies only to depth-stencil image attachments.
        AttachmentLoadAction m_loadActionStencil = AttachmentLoadAction::Load;

        /// The stencil store action. Applies only to depth-stencil image attachments.
        AttachmentStoreAction m_storeActionStencil = AttachmentStoreAction::Store;
    };
}