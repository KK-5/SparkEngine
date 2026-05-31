/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "IndirectBufferLayout.h"

#include <EASTLEX/hash.h>
#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    size_t IndirectCommandDescriptor::GetHash(size_t seed) const
    {
        size_t hash = eastl::hash<const IndirectCommandDescriptor*>()(this);
        eastl::hash_combine_raw(hash, seed);
        return hash;
    }

    bool IndirectBufferLayout::IsFinalized() const
    {
        return m_hash != 0;
    }

    bool IndirectBufferLayout::Finalize()
    {
        if (!ValidateFinalizeState(ValidateFinalizeStateExpect::NotFinalized))
        {
            return false;
        }

        // Calculate the hash and get the main command type while
        // iterating through the commands.
        m_type = IndirectBufferLayoutType::Undefined;
        m_hash = 0;
        for (uint32_t i = 0; i < m_commands.size(); ++i)
        {
            const auto& commandDesc = m_commands[i];

            eastl::hash_combine_raw(m_hash, commandDesc.GetHash());
            m_idReflectionForCommands[static_cast<uint64_t>(commandDesc.GetHash())] = IndirectCommandIndex(i);
            bool result = true;
            switch (commandDesc.m_type)
            {
            case IndirectCommandType::Draw:
                result = SetType(IndirectBufferLayoutType::LinearDraw);
                break;
            case IndirectCommandType::DrawIndexed:
                result = SetType(IndirectBufferLayoutType::IndexedDraw);
                break;
            case IndirectCommandType::Dispatch:
                result = SetType(IndirectBufferLayoutType::Dispatch);
                break;
            case IndirectCommandType::DispatchRays:
                result = SetType(IndirectBufferLayoutType::DispatchRays);
                break;
                default:
                    // Skip command
                    break;
            }

            if (!result)
            {
                return false;
            }
        }

        eastl::hash_combine(m_hash, static_cast<uint32_t>(m_type));

        if (Validation::isEnabled)
        {
            if (m_type == IndirectBufferLayoutType::Undefined)
            {
                ASSERT(false, "Missing Draw, DrawIndexed or Dispatch command in the layout.");
                return false;
            }
        }
        return true;
    }

    size_t IndirectBufferLayout::GetHash([[maybe_unused]]size_t seed) const
    {
        return m_hash;
    }

    bool IndirectBufferLayout::AddIndirectCommand(const IndirectCommandDescriptor& command)
    {
        if (!ValidateCommand(command))
        {
            return false;
        }

        m_commands.push_back(command);
        return true;
    }

    eastl::span<const IndirectCommandDescriptor> IndirectBufferLayout::GetCommands() const
    {
        if (!ValidateFinalizeState(ValidateFinalizeStateExpect::Finalized))
        {
            return eastl::span<const IndirectCommandDescriptor>();
        }
        return m_commands;
    }

    IndirectCommandIndex IndirectBufferLayout::FindCommandIndex(const IndirectCommandDescriptor& command) const
    {
        auto findIt = m_idReflectionForCommands.find(static_cast<uint64_t>(command.GetHash()));
        return findIt == m_idReflectionForCommands.end() ? InvalidIndirectCommandIndex : findIt->second;
    }

    IndirectBufferLayoutType IndirectBufferLayout::GetType() const
    {
        return m_type;
    }

    bool IndirectBufferLayout::ValidateFinalizeState(ValidateFinalizeStateExpect expect) const
    {
        if (Validation::isEnabled)
        {
            if (expect == ValidateFinalizeStateExpect::Finalized && !IsFinalized())
            {
                ASSERT(false, "IndirectBufferLayout must be finalized when calling this method.");
                return false;
            }
            else if (expect == ValidateFinalizeStateExpect::NotFinalized && IsFinalized())
            {
                ASSERT(false, "IndirectBufferLayout cannot be finalized when calling this method.");
                return false;
            }
        }
        return true;
    }

    bool IndirectBufferLayout::ValidateCommand(const IndirectCommandDescriptor& command) const
    {
        if (Validation::isEnabled)
        {
            if (IsFinalized())
            {
                ASSERT(false, "Layout already finalized");
                return false;
            }

            switch (command.m_type)
            {
            case IndirectCommandType::Draw:
            case IndirectCommandType::DrawIndexed:
            case IndirectCommandType::Dispatch:
            case IndirectCommandType::DispatchRays:
            case IndirectCommandType::IndexBufferView:
            case IndirectCommandType::RootConstants:
            case IndirectCommandType::VertexBufferView:
                if (eastl::find(m_commands.begin(), m_commands.end(), command) != m_commands.end())
                {
                    ASSERT(false, "Duplicated command {}.", static_cast<uint32_t>(command.m_type));
                    return false;
                }
                break;
            default:
                ASSERT(false, "Invalid command type {}.", static_cast<uint32_t>(command.m_type));
                return false;
            }
        }

        return true;
    }

    bool IndirectBufferLayout::SetType(IndirectBufferLayoutType type)
    {
        if (Validation::isEnabled)
        {
            if (m_type != IndirectBufferLayoutType::Undefined)
            {
                ASSERT(false, "Trying to set a layout type ({}) when one is already set ({})", static_cast<uint32_t>(type), static_cast<uint32_t>(m_type));
                return false;
            }
        }

        m_type = type;
        return true;
    }
}