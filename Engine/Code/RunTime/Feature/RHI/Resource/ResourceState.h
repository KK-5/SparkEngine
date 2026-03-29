/*
 * Modified by SparkEngine in 2025
 *  -- Vulkan-style resource state: (AttachmentUsage, AttachmentAccess) pair
 *     replaces D3D12-style bitmask enum. Backend derives native states from this.
 */
#pragma once

#include <RHI/Attachment/AttachmentEnums.h>

namespace Spark::RHI
{
    class Buffer;
    class Image;

    /*
    DX12 Conversion
        (AttachmentUsage, AttachmentAccess) -> D3D12_RESOURCE_STATES
        ------------------------------------------------------------------
        (InputAssembly, Read)  → VERTEX_AND_CONSTANT_BUFFER | INDEX_BUFFER
        (Shader, Read)         → VCB | PIXEL_SR | NON_PIXEL_SR
        (Shader, Write)        → UNORDERED_ACCESS
        (Copy, Read)           → COPY_SOURCE
        (Copy, Write)          → COPY_DEST
        (DepthStencil, Read)   → DEPTH_READ
        (DepthStencil, Write)  → DEPTH_WRITE
        (RenderTarget, *)      → RENDER_TARGET
        (Indirect, *)          → INDIRECT_ARGUMENT
        (Uninitialized, *)     → COMMON
    AttachmentStage is unused by DX12
    */

    /// Tracks the current usage state of a resource.
    /// Based on Vulkan's synchronization model: Usage (what) + Access (read/write).
    struct ResourceState
    {
        AttachmentUsage  m_usage  = AttachmentUsage::Uninitialized;
        AttachmentAccess m_access = AttachmentAccess::Unknown;

        bool operator==(const ResourceState& other) const
        {
            return m_usage == other.m_usage && m_access == other.m_access;
        }
        bool operator!=(const ResourceState& other) const { return !(*this == other); }
    };

    struct BufferBarrier
    {
        Buffer*          m_buffer   = nullptr;
        AttachmentUsage  m_srcUsage = AttachmentUsage::Uninitialized;
        AttachmentUsage  m_dstUsage = AttachmentUsage::Uninitialized;
        AttachmentAccess m_srcAccess = AttachmentAccess::Unknown;
        AttachmentAccess m_dstAccess = AttachmentAccess::Unknown;
        AttachmentStage  m_srcStage = AttachmentStage::Any;
        AttachmentStage  m_dstStage = AttachmentStage::Any;
    };

    struct ImageBarrier
    {
        Image*           m_image    = nullptr;
        AttachmentUsage  m_oldUsage = AttachmentUsage::Uninitialized;
        AttachmentUsage  m_newUsage = AttachmentUsage::Uninitialized;
        AttachmentAccess m_srcAccess = AttachmentAccess::Unknown;
        AttachmentAccess m_dstAccess = AttachmentAccess::Unknown;
        AttachmentStage  m_srcStage = AttachmentStage::Any;
        AttachmentStage  m_dstStage = AttachmentStage::Any;
    };
}
