/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/RHILimits.h>
#include "IndirectArguments.h"

namespace Spark::RHI
{
    constexpr uint32_t DivideAndRoundUp(uint32_t value, uint32_t alignment)
    {
        if (alignment == 0)
        {
            return 0;
        }

        return value / alignment + (value % alignment != 0);
    }

    struct DispatchGroupCount
    {
        uint32_t m_x = 1;
        uint32_t m_y = 1;
        uint32_t m_z = 1;
    };

    //! Arguments used when submitting a (direct) dispatch call into a CommandList.
    struct DispatchDirect
    {
        DispatchDirect() = default;

        explicit DispatchDirect(DispatchGroupCount groupCount)
            : m_groupCount(groupCount)
        {}

        DispatchDirect(
            uint32_t totalNumberOfThreadsX,
            uint32_t totalNumberOfThreadsY,
            uint32_t totalNumberOfThreadsZ,
            uint16_t threadsPerGroupX,
            uint16_t threadsPerGroupY,
            uint16_t threadsPerGroupZ)
            : DispatchDirect(DispatchGroupCount{
                DivideAndRoundUp(totalNumberOfThreadsX, threadsPerGroupX),
                DivideAndRoundUp(totalNumberOfThreadsY, threadsPerGroupY),
                DivideAndRoundUp(totalNumberOfThreadsZ, threadsPerGroupZ) })
        {}

        uint32_t GetNumberOfGroupsX() const
        {
            return m_groupCount.m_x;
        }

        uint32_t GetNumberOfGroupsY() const
        {
            return m_groupCount.m_y;
        }

        uint32_t GetNumberOfGroupsZ() const
        {
            return m_groupCount.m_z;
        }

    private:
        DispatchGroupCount m_groupCount;
    };

    //! Arguments used when submitting an indirect dispatch call into a CommandList: one
    //! DispatchIndirectCommand at m_byteOffset of m_buffer. A dispatch reads a single record
    //! and has no count buffer in either API.
    struct DispatchIndirect
    {
        DispatchIndirect() = default;

        DispatchIndirect(const Buffer* buffer, uint64_t byteOffset)
            : m_buffer(buffer)
            , m_byteOffset(byteOffset)
        {}

        const Buffer* m_buffer     = nullptr;
        uint64_t      m_byteOffset = 0;
    };

    enum class DispatchType : uint8_t
    {
        Direct = 0, // A dispatch call where the arguments are pass directly to the submit function.
        Indirect    // An indirect dispatch call that uses a buffer that contains the arguments.
    };

    //! Encapsulates the arguments that are specific to a type of dispatch.
    //! It uses a union to be able to store all possible arguments.
    struct DispatchArguments
    {
        DispatchArguments() : DispatchArguments(DispatchDirect{}) {}

        DispatchArguments(const DispatchDirect& direct)
            : m_type{ DispatchType::Direct }
            , m_direct{ direct }
        {}

        DispatchArguments(const DispatchIndirect& indirect)
            : m_type{ DispatchType::Indirect }
            , m_indirect{ indirect }
        {}

        DispatchType m_type;
        union
        {
            /// Arguments for a direct dispatch.
            DispatchDirect m_direct;
            /// Arguments for an indirect dispatch.
            DispatchIndirect m_indirect;
        };
    };

    //! Encapsulates all the necessary information for doing a dispatch call.
    //! This includes all common arguments for the different dispatch type, plus
    //! arguments that are specific to a type.
    struct DispatchItem
    {
        DispatchItem() = default;

        /// Arguments specific to a dispatch type.
        DispatchArguments m_arguments;
    };
}
