/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/numeric_limits.h>

#include <RHI/Base.h>

namespace Spark::RHI
{
    //! Command types that can be used when doing Indirect Rendering.
    enum class IndirectCommandType : uint32_t
    {
        Draw,               // A Draw operation
        DrawIndexed,        // An Indexed Draw operation
        Dispatch,           // A Dispatch operation
        DispatchRays,       // A Ray Tracing operation
        VertexBufferView,   // Set a Vertex Buffer View into a specific slot
        IndexBufferView,    // Set the Index Buffer View
        RootConstants       // Set the values of all Inline Constants
    };

    //! Indirect Rendering tiers that define which commands are supported by the RHI implementation.
    //! Since it's a tier system, TierX supports everything TierY does, if X > Y.
    enum class IndirectCommandTiers : uint32_t
    {
        Tier0, // No support for indirect commands.
        Tier1, // Supports Draw, DrawIndexed and Dispatch commands. 
        Tier2  // Supports everything in Tier1 + Vertex Buffer View, Index Buffer View and Inline Constants commands.
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::IndirectCommandTiers, uint32_t);

    //! Arguments when setting an indirect Vertex Buffer View command.
    struct IndirectBufferViewArguments
    {
        /// The vertex stream slot that the command will update.
        uint32_t m_slot = 0;
    };

    inline bool operator==(const IndirectBufferViewArguments& lhs, const IndirectBufferViewArguments& rhs)
    {
        return lhs.m_slot == rhs.m_slot;
    }

    //! Describes one indirect command that is part of an indirect layout.
    struct IndirectCommandDescriptor
    {
        IndirectCommandDescriptor() : IndirectCommandDescriptor(IndirectCommandType::Draw)
        {}

        IndirectCommandDescriptor(IndirectCommandType type)
            : m_type(type)
        {}

        IndirectCommandDescriptor(const IndirectBufferViewArguments& arguments)
            : m_type(IndirectCommandType::VertexBufferView)
            , m_vertexBufferArgs(arguments)
        {}

        size_t GetHash(size_t seed = 0) const;

        IndirectCommandType m_type;
        /// Arguments when the command is a Vertex Buffer View type.
        IndirectBufferViewArguments m_vertexBufferArgs;
    };

    inline bool operator==(const IndirectCommandDescriptor& lhs, const IndirectCommandDescriptor& rhs)
    {
        return
            // Check that the type is the same.
            lhs.m_type == rhs.m_type &&
            // If it's a vertex command, check that the arguments are also the same.
            (lhs.m_type != IndirectCommandType::VertexBufferView || lhs.m_vertexBufferArgs == rhs.m_vertexBufferArgs);
    }

    //! The type of the main command of an IndirectBufferLayout.
    //! Each layout must have one, and only one main command.
    //! A main command is a draw, drawIndexed or dispatch command.
    enum class IndirectBufferLayoutType : uint8_t
    {
        Undefined = 0,
        LinearDraw,  // The main command is a draw
        IndexedDraw, // The main command is an indexed draw
        Dispatch,    // The main command is a dispatch
        DispatchRays,// The main command is a ray dispatch
    };

    /// Index of a command in an IndirectBufferLayout.
    using IndirectCommandIndex = uint32_t;

    constexpr static IndirectCommandIndex InvalidIndirectCommandIndex = eastl::numeric_limits<uint32_t>::max();

    //! Describes a sequence of indirect commands of an Indirect Buffer.
    //! The order in which the commands are added to the layout is preserved
    //! and defines the offset of the command.
    //!
    //! To use the class, add commands using AddIndirectCommand, and call Finalize to
    //! complete the construction of the layout.
    struct IndirectBufferLayout
    {
    public:
        IndirectBufferLayout() = default;

        bool IsFinalized() const;

        //! Finalizes the layout for access. Must be called prior to usage or serialization.
        //! It is not permitted to mutate the layout once Finalize is called.
        bool Finalize();

        /// Returns the hash computed in Finalize.
        size_t GetHash(size_t seed = 0) const;

        //! Adds a new indirect command to the end of the layout.
        //! This must be called before the layout is finalized.
        //! @param command The descriptor of the command to add.
        //! @return True if the command was successfully added, False otherwise.
        bool AddIndirectCommand(const IndirectCommandDescriptor& command);

        /// Returns the list of indirect commands of the layout. Must be called after the layout is finalized.
        eastl::span<const IndirectCommandDescriptor> GetCommands() const;

        //! Returns the position of a command.
        //! Must be called after the layout is finalized.
        //! @param command The descriptor of the command to find.
        //! @return The position of the command in the layout. If the command is not part of the
        //!         layout, a null index is returned.
        IndirectCommandIndex FindCommandIndex(const IndirectCommandDescriptor& command) const;

        //! Returns the type of the main command of the layout. Every layout has one main command.
        //! Must be called after the layout is finalized.
        IndirectBufferLayoutType GetType() const;

    private:
        enum class ValidateFinalizeStateExpect
        {
            NotFinalized = 0,
            Finalized
        };

        bool ValidateFinalizeState(ValidateFinalizeStateExpect expect) const;
        bool ValidateCommand(const IndirectCommandDescriptor& command) const;
        bool SetType(IndirectBufferLayoutType type);

        // List of indirect commands of the layout.
        eastl::vector<IndirectCommandDescriptor> m_commands;
        // Maps an IndirectCommandDescriptor (using the hash) to a position in the command list.
        eastl::unordered_map<uint64_t, IndirectCommandIndex> m_idReflectionForCommands;
        // Main command type of the layout. Value is determinate during the Finalize phase.
        IndirectBufferLayoutType m_type = IndirectBufferLayoutType::Undefined;
        // Hash of the layout. Value is calculated during the Finalize phase.
        size_t m_hash { 0 };
    };
}