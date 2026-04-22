/*
 * SparkEngine RHI - slim render-target format layout for graphics PSO creation
 * under dynamic rendering.
 *
 * Carries exactly what VkPipelineRenderingCreateInfo and D3D12 PSO's
 * RTVFormats / DSVFormat need. No VkRenderPass, no subpass, no input attachment
 * concepts. Replaces the subpass-heavy RenderAttachmentConfiguration for new
 * pipeline code; old callers (examples, legacy paths) can continue to use
 * RenderAttachmentConfiguration until migrated.
 */
#pragma once

#include <EASTL/array.h>
#include <EASTLEX/hash.h>

#include <RHI/RHILimits.h>
#include <RHI/Format.h>

namespace Spark::RHI
{
    struct RenderTargetLayout
    {
        /// Number of valid color attachments.
        uint32_t m_colorAttachmentCount = 0;

        /// Per-attachment color formats.
        eastl::array<Format, Limits::Pipeline::AttachmentColorCountMax> m_colorFormats = { {} };

        /// Per-attachment MSAA resolve target formats. Format::Unknown = no resolve.
        eastl::array<Format, Limits::Pipeline::AttachmentColorCountMax> m_resolveFormats = { {} };

        /// Depth-stencil format. Format::Unknown disables the depth-stencil attachment.
        Format m_depthStencilFormat = Format::Unknown;

        /// Variable-rate shading rate image format. Format::Unknown disables VRS.
        Format m_shadingRateFormat = Format::Unknown;

        /// MSAA sample count (color / depth-stencil must match). 1 = no MSAA.
        uint32_t m_sampleCount = 1;

        /// Vulkan multiview mask. 0 disables multiview (default). Ignored by DX12.
        uint32_t m_viewMask = 0;

        /// Sentinel for incremental migration: true means caller hasn't set this layout
        /// and the backend should fall back to the legacy RenderAttachmentConfiguration.
        bool IsEmpty() const
        {
            return m_colorAttachmentCount == 0 && m_depthStencilFormat == Format::Unknown;
        }

        size_t GetHash() const
        {
            size_t seed = 0;
            eastl::hash_combine(seed, m_colorAttachmentCount);
            for (uint32_t i = 0; i < m_colorAttachmentCount; ++i)
            {
                eastl::hash_combine(seed, static_cast<uint32_t>(m_colorFormats[i]));
                eastl::hash_combine(seed, static_cast<uint32_t>(m_resolveFormats[i]));
            }
            eastl::hash_combine(seed, static_cast<uint32_t>(m_depthStencilFormat));
            eastl::hash_combine(seed, static_cast<uint32_t>(m_shadingRateFormat));
            eastl::hash_combine(seed, m_sampleCount);
            eastl::hash_combine(seed, m_viewMask);
            return seed;
        }

        bool operator==(const RenderTargetLayout& other) const
        {
            if (m_colorAttachmentCount != other.m_colorAttachmentCount ||
                m_depthStencilFormat != other.m_depthStencilFormat ||
                m_shadingRateFormat != other.m_shadingRateFormat ||
                m_sampleCount != other.m_sampleCount ||
                m_viewMask != other.m_viewMask)
            {
                return false;
            }
            for (uint32_t i = 0; i < m_colorAttachmentCount; ++i)
            {
                if (m_colorFormats[i] != other.m_colorFormats[i] ||
                    m_resolveFormats[i] != other.m_resolveFormats[i])
                {
                    return false;
                }
            }
            return true;
        }

        bool operator!=(const RenderTargetLayout& other) const
        {
            return !(*this == other);
        }
    };
}
