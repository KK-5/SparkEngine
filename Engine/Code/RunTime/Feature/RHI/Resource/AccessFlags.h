/*
 * SparkEngine
 *  -- AccessFlags: fine-grained, set-valued resource access state. Replaces the
 *     (AttachmentUsage, AttachmentAccess) pair; bits are OR-able so concurrent
 *     reads combine. Follows Vulkan synchronization2 VkAccessFlags2, and also
 *     drives layout / D3D12_RESOURCE_STATES derivation in the backend.
 */
#pragma once

#include <cstdint>
#include <Math/Bit.h>

namespace Spark::RHI
{
    enum class AccessFlags : uint32_t
    {
        //! No access. As a resource's state: nothing accesses it and its contents are kept,
        //! which is where acquiring it leaves it. A barrier starts from it, none ends in it.
        None                 = 0,

        // reads [0,15]
        IndirectRead         = BIT(0),
        VertexIndexInput     = BIT(1),
        ConstantBufferRead   = BIT(2),
        ShaderSampledRead    = BIT(3),
        ShaderStorageRead    = BIT(4),
        DepthStencilRead     = BIT(5),
        ColorAttachmentRead  = BIT(6),
        TransferRead         = BIT(7),
        ResolveRead          = BIT(8),
        PredicationRead      = BIT(9),
        ShadingRateRead      = BIT(10),
        InputAttachmentRead  = BIT(11),
        AccelStructRead      = BIT(12),
        //! Vertices, indices or instances read by an acceleration structure build.
        AccelStructBuildInputRead = BIT(13),

        // writes [16,23]
        ShaderStorageWrite   = BIT(16),
        ColorAttachmentWrite = BIT(17),
        DepthStencilWrite    = BIT(18),
        TransferWrite        = BIT(19),
        ResolveWrite         = BIT(20),
        AccelStructWrite     = BIT(21),
        //! A build's scratch memory, read and written by it.
        AccelStructScratch   = BIT(22),

        // states standing in for a layout [24+]
        Present              = BIT(24),
        //! The contents need not be kept: a barrier from it discards them. Source side only;
        //! the read / write bits beside it are the memory's previous accesses (an aliased
        //! resource's previous occupant), still to be synchronized against.
        Undefined            = BIT(25),
        //! Newly made, in memory of its own that nothing has accessed: it has no contents
        //! yet. Source side only. Apart from Undefined, which a backend may discard from and
        //! which names a previous use; apart from None, whose contents a barrier keeps.
        Initial              = BIT(26),

        ReadMask =
            IndirectRead | VertexIndexInput | ConstantBufferRead | ShaderSampledRead |
            ShaderStorageRead | DepthStencilRead | ColorAttachmentRead | TransferRead |
            ResolveRead | PredicationRead | ShadingRateRead | InputAttachmentRead | AccelStructRead |
            AccelStructBuildInputRead,

        WriteMask =
            ShaderStorageWrite | ColorAttachmentWrite | DepthStencilWrite |
            TransferWrite | ResolveWrite | AccelStructWrite | AccelStructScratch,
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::AccessFlags, uint32_t);

    inline bool HasWrite(AccessFlags a)
    {
        return CheckBitsAny(a, AccessFlags::WriteMask);
    }

    inline bool HasRead(AccessFlags a)
    {
        return CheckBitsAny(a, AccessFlags::ReadMask);
    }

    //! Two accesses to one resource at the same time conflict unless both read, or together
    //! they are exactly shader storage read+write.
    inline bool HasAccessConflict(AccessFlags lhs, AccessFlags rhs)
    {
        const AccessFlags combined = lhs | rhs;
        return HasWrite(combined)
            && combined != (AccessFlags::ShaderStorageRead | AccessFlags::ShaderStorageWrite);
    }

    inline uint8_t CountAccessBits(AccessFlags a)
    {
        return CountBitsSet(static_cast<uint32_t>(a));
    }
}
