#pragma once

#include <RHI/Command/CopyItem.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>

namespace Spark::Render
{
    //! Declarative copy "recipe" produced by a pass's Processor (upper layer).
    //! References source / destination by attachment slot name — never by a
    //! resolved resource — so it can be filled before the render graph
    //! materializes transient resources. The owning pass is identified by the
    //! PassTag the Processor stamps on the CopyRequest entity (no m_pass field);
    //! CompilePassCopyRequests<PassTag> resolves the slots into an RHI::CopyItem.
    //!
    //! The Processor fills m_template's type and ALL scalar parameters (origins,
    //! offsets, bytesPerRow/Image, format, size, subresources) and leaves only the
    //! resource pointers null. The compiler patches in nothing but the resolved
    //! Image / Buffer pointers according to m_template.m_type — it never
    //! reverse-derives a scalar (e.g. size) from a resolved resource, so that the
    //! pass stays ignorant of upper-layer intent:
    //!   - Image          : source/dest slots are image attachments
    //!   - Buffer         : source/dest slots are buffer attachments
    //!   - BufferToImage  : source slot is a buffer, dest slot is an image
    //!   - ImageToBuffer  : source slot is an image, dest slot is a buffer
    struct CopyRequest
    {
        RHI::InputName m_sourceSlot;        //!< Source attachment slot on the owning pass.
        RHI::InputName m_destSlot;          //!< Destination attachment slot on the owning pass.

        RHI::CopyItem  m_template;          //!< Type + scalar params; resource pointers filled at compile.
    };
}
