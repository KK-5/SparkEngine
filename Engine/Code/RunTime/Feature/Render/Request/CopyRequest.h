#pragma once

#include <RHI/Command/CopyItem.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>

#include <Pass/Pass.h>

namespace Spark::Render
{
    //! Declarative copy "recipe" produced by a pass's Processor (upper layer).
    //! References source / destination by attachment slot name — never by a
    //! resolved resource — so it can be filled before the render graph
    //! materializes transient resources. RenderGraphCompiler::CompileCopyRequests
    //! resolves the slots within m_pass into a compiled RHI::CopyItem.
    //!
    //! The Processor fills m_template's type and all scalar parameters (origins,
    //! offsets, bytesPerRow/Image, format, size, subresources) and leaves the
    //! resource pointers null. The compiler patches in the resolved Image / Buffer
    //! pointers according to m_template.m_type:
    //!   - Image          : source/dest slots are image attachments
    //!   - Buffer         : source/dest slots are buffer attachments
    //!   - BufferToImage  : source slot is a buffer, dest slot is an image
    //!   - ImageToBuffer  : source slot is an image, dest slot is a buffer
    //! For image sources, a zero Size means "full source image" — the compiler
    //! fills it from the resolved image's descriptor.
    struct CopyRequest
    {
        Pass           m_pass = NullPass;   //!< Owning pass (stable handle from FindPass at init).

        RHI::InputName m_sourceSlot;        //!< Source attachment slot on m_pass.
        RHI::InputName m_destSlot;          //!< Destination attachment slot on m_pass.

        RHI::CopyItem  m_template;          //!< Type + scalar params; resource pointers filled at compile.
    };
}
