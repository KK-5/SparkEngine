/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Object/Object.h>

#include <RHI/Base.h>

namespace Spark::RHI
{
    class Buffer;
    class IndirectBufferSignature;
    class IndexBufferView;

    //! IndirectBufferWriter is a helper class to write indirect commands
    //! to a buffer or a memory location in a platform independent way. Different APIs may
    //! have different layouts for the arguments of an indirect command. This class provides
    //! a secure and simple way to write the commands without worrying about API differences.
    //!
    //! It also provides basic checks, like trying to write more commands than allowed, or
    //! writing commands that are not specified in the layout.
    class IndirectBufferWriter : public Object
    {
    public:
        virtual ~IndirectBufferWriter() = default;

        //! Initialize the DeviceIndirectBufferWriter to write commands into a buffer.
        //! @param buffer The buffer where to write the commands. Any previous values for the specified range will be overwritten.
        //!               The buffer must be big enough to contain the max number of sequences.
        //! @param byteOffset The offset into the buffer.
        //! @param byteStride The stride between command sequences. Must be larger than the stride calculated from the signature.
        //! @param maxCommandSequences The max number of sequences that the DeviceIndirectBufferWriter can write.
        //! @param signature Signature of the indirect buffer.
        //! @return A result code denoting the status of the call. If successful, the DeviceIndirectBufferWriter is considered
        //!      initialized and is able to service write requests. If failure, the DeviceIndirectBufferWriter remains uninitialized.
        ResultCode Init(Buffer& buffer, size_t byteOffset, uint32_t byteStride, uint32_t maxCommandSequences, const IndirectBufferSignature& signature);

        //! Initialize the DeviceIndirectBufferWriter to write commands into a memory location.
        //! @param memoryPtr The memory location where the commands will be written. Must not be null.
        //! @param byteStride The stride between command sequences. Must be larger than the stride calculated from the signature.
        //! @param maxCommandSequences The max number of sequences that the DeviceIndirectBufferWriter can write.
        //! @param signature Signature of the indirect buffer.
        //! @return A result code denoting the status of the call. If successful, the DeviceIndirectBufferWriter is considered
        //!      initialized and is able to service write requests. If failure, the DeviceIndirectBufferWriter remains uninitialized.
        ResultCode Init(void* memoryPtr, uint32_t byteStride, uint32_t maxCommandSequences, const IndirectBufferSignature& signature);

        //! Writes a vertex buffer view command into the current sequence.
        //! @param slot The stream buffer slot that the view will set.
        //! @param view The DeviceStreamBufferView that will be set.
        //! @return A pointer to the DeviceIndirectBufferWriter object (this).
        // IndirectBufferWriter* SetVertexView(uint32_t slot, const StreamBufferView& view);

        //! Writes an index buffer view command into the current sequence.
        //! @param view The DeviceIndexBufferView that will be set.
        //! @return A pointer to the DeviceIndirectBufferWriter object (this).
        IndirectBufferWriter* SetIndexView(const IndexBufferView& view);

        //! Writes a draw command into the current sequence.
        //! @param arguments The draw arguments that will be written.
        //! @return A pointer to the DeviceIndirectBufferWriter object (this).
        // IndirectBufferWriter* Draw(const DrawLinear& arguments, const RHI::DrawInstanceArguments& drawInstanceArgs);

        //! Writes a draw indexed command into the current sequence.
        //! @param arguments The draw indexed arguments that will be written.
        //! @return A pointer to the DeviceIndirectBufferWriter object (this).
        // IndirectBufferWriter* DrawIndexed(const DrawIndexed& arguments, const RHI::DrawInstanceArguments& drawInstanceArgs);

        //! Writes a dispatch command into the current sequence.
        //! @param arguments The dispatch arguments that will be written.
        //! @return A pointer to the DeviceIndirectBufferWriter object (this).
        // IndirectBufferWriter* Dispatch(const DispatchDirect& arguments);

    };
}