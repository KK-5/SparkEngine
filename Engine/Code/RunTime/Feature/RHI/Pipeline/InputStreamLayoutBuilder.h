/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "InputStreamLayout.h"

namespace Spark::RHI
{
    //! Provides a convenient way to construct InputStreamLayout objects, which describes
    //! the input assembly stream buffer layout for the pipeline state.
    //!
    //! The general usage includes adding one or more stream buffer descriptors, and 
    //! adding one or more channels descriptors to each buffer. Examples are shown below.
    //!
    //! 1) Individual Stream Buffers - each stream channel is contained in a separate buffers
    //!    @code
    //!      RHI::InputStreamLayoutBuilder layoutBuilder;
    //!      layoutBuilder.AddBuffer()->Channel("POSITION", RHI::Format::R32G32B32_FLOAT);
    //!      layoutBuilder.AddBuffer()->Channel("COLOR", RHI::Format::R32G32B32A32_FLOAT);
    //!      layoutBuilder.AddBuffer()->Channel("UV", RHI::Format::R32G32_FLOAT);
    //!      layout = layoutBuilder.End();
    //!    @endcode
    //!
    //! 2) Interleaved Stream Buffers - a single buffer contains all stream channels
    //!    @code
    //!      RHI::InputStreamLayoutBuilder layoutBuilder;
    //!      layoutBuilder.AddBuffer()
    //!          ->Channel("POSITION", RHI::Format::R32G32B32_FLOAT)
    //!          ->Channel("COLOR", RHI::Format::R8G8B8A8_UNORM)
    //!          ->Channel("UV", RHI::Format::R32G32_FLOAT);
    //!      layout = layoutBuilder.End();
    //!    @endcode
    //!
    //! 3) Multiple Interleaved Stream Buffers - multiple buffers with multiple channels
    //!    @code
    //!      RHI::InputStreamLayoutBuilder layoutBuilder;
    //!      layoutBuilder.AddBuffer()
    //!          ->Channel("POSITION", RHI::Format::R32G32B32_FLOAT)
    //!          ->Channel("COLOR", RHI::Format::R32G32B32A32_FLOAT);
    //!      layoutBuilder.AddBuffer()
    //!          ->Channel("UV0", RHI::Format::R32G32_FLOAT)
    //!          ->Channel("UV1", RHI::Format::R32G32_FLOAT);
    //!      layout = layoutBuilder.End();
    //!    @endcode

    class InputStreamLayoutBuilder final
    {
    public:
        class BufferDescriptorBuilder
        {
            friend class InputStreamLayoutBuilder;

        public:
            BufferDescriptorBuilder* Channel(eastl::string_view name, uint32_t index, Format format);

            BufferDescriptorBuilder* Channel(const ShaderSemantic& semantic, Format format);

            BufferDescriptorBuilder* Padding(uint32_t byteCount);

        private:
            uint32_t m_bufferIndex = 0;
            uint32_t m_byteOffset = 0;
            eastl::fixed_vector<StreamChannelDescriptor, Limits::Pipeline::StreamChannelCountMax> m_channelDescriptors;
            StreamBufferDescriptor m_bufferDescriptor;
        };

        InputStreamLayoutBuilder();

        void Begin();

        void SetTopology(PrimitiveTopology topology);

        BufferDescriptorBuilder* AddBuffer(StreamStepFunction stepFunction = StreamStepFunction::PerVertex, uint32_t stepRate = 1);

        InputStreamLayout End();

    private:
        PrimitiveTopology m_topology = PrimitiveTopology::TriangleList;

        eastl::fixed_vector<BufferDescriptorBuilder, Limits::Pipeline::StreamCountMax> m_bufferDescriptorBuilders;
        BufferDescriptorBuilder m_dummyBufferDescriptorBuilder;
    };
}