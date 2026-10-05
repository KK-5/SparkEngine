/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- IndirectArguments has been changed from a template class to a normal class, with template parameters fixed as Buffer and IndirectBufferView.
 * Modified by SparkEngine in 2026
 *  -- Names the buffer directly instead of an IndirectBufferView: the record layouts are
 *     fixed (IndirectCommands.h), so there is no signature to carry and no stride to choose.
 */

#pragma once

#include <cstdint>

#include "IndirectCommands.h"

namespace Spark::RHI
{
    class Buffer;

    //! Where an indirect draw finds its records (see IndirectCommands.h for their layout).
    //! Which record type m_buffer holds is said by the draw type that carries this.
    struct IndirectArguments
    {
        //! Holds the records, sizeof(record) apart, the first one at m_byteOffset.
        const Buffer* m_buffer     = nullptr;
        uint64_t      m_byteOffset = 0;

        //! Without a count buffer: exactly how many records are executed.
        //! With one: the upper bound; the uint32 at m_countByteOffset says how many.
        uint32_t      m_maxCount = 0;

        const Buffer* m_countBuffer     = nullptr;
        uint64_t      m_countByteOffset = 0;
    };
}
