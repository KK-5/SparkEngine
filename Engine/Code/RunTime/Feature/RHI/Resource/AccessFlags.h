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

        // writes [16,23]
        ShaderStorageWrite   = BIT(16),
        ColorAttachmentWrite = BIT(17),
        DepthStencilWrite    = BIT(18),
        TransferWrite        = BIT(19),
        ResolveWrite         = BIT(20),
        AccelStructWrite     = BIT(21),

        // terminal / exclusive [24+]
        Present              = BIT(24),

        ReadMask =
            IndirectRead | VertexIndexInput | ConstantBufferRead | ShaderSampledRead |
            ShaderStorageRead | DepthStencilRead | ColorAttachmentRead | TransferRead |
            ResolveRead | PredicationRead | ShadingRateRead | InputAttachmentRead | AccelStructRead,

        WriteMask =
            ShaderStorageWrite | ColorAttachmentWrite | DepthStencilWrite |
            TransferWrite | ResolveWrite | AccelStructWrite,
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

    inline uint8_t CountAccessBits(AccessFlags a)
    {
        return CountBitsSet(static_cast<uint32_t>(a));
    }
}
