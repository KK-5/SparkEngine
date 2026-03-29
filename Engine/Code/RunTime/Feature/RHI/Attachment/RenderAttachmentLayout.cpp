/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "RenderAttachmentLayout.h"

#include <EASTLEX/hash.h>

namespace Spark::RHI
{
    bool RenderAttachmentDescriptor::IsValid() const
    {
        return m_attachmentIndex != InvalidRenderAttachmentIndex;
    }

    bool RenderAttachmentDescriptor::operator==(const RenderAttachmentDescriptor& other) const
    {
        return IsEqual(other, true);
    }

    bool RenderAttachmentDescriptor::IsEqual(const RenderAttachmentDescriptor& other, const bool compareLoadStoreAction) const
    {
        // clang-format off
        return (m_attachmentIndex == other.m_attachmentIndex) && 
               (m_resolveAttachmentIndex == other.m_resolveAttachmentIndex) &&
               (!compareLoadStoreAction || (m_loadStoreAction == other.m_loadStoreAction)) && 
               (m_attachmentAccess == other.m_attachmentAccess) &&
               (m_attachmentStage == other.m_attachmentStage);
        // clang-format on
    }

    bool RenderAttachmentDescriptor::operator!=(const RenderAttachmentDescriptor& other) const
    {
        return !(*this == other);
    }

    bool SubpassInputDescriptor::operator==(const SubpassInputDescriptor& other) const
    {
        return (m_attachmentIndex == other.m_attachmentIndex)
            && (m_aspectFlags == other.m_aspectFlags)
            && (m_attachmentAccess == other.m_attachmentAccess)
            && (m_attachmentStage == other.m_attachmentStage)
            ;
    }

    bool SubpassInputDescriptor::operator!=(const SubpassInputDescriptor& other) const
    {
        return !(*this == other);
    }

    bool SubpassRenderAttachmentLayout::operator==(const SubpassRenderAttachmentLayout& other) const
    {
        return IsEqual(other, true);
    }

    bool SubpassRenderAttachmentLayout::IsEqual(const SubpassRenderAttachmentLayout& other, const bool compareLoadStoreAction) const
    {
        if ((m_rendertargetCount != other.m_rendertargetCount) || (m_subpassInputCount != other.m_subpassInputCount) ||
            (!m_depthStencilDescriptor.IsEqual(other.m_depthStencilDescriptor, compareLoadStoreAction)))
        {
            return false;
        }

        for (uint32_t i = 0; i < m_rendertargetCount; ++i)
        {
            if (!m_rendertargetDescriptors[i].IsEqual(other.m_rendertargetDescriptors[i], compareLoadStoreAction))
            {
                return false;
            }
        }

        for (uint32_t i = 0; i < m_subpassInputCount; ++i)
        {
            if (m_subpassInputDescriptors[i] != other.m_subpassInputDescriptors[i])
            {
                return false;
            }
        }

        return true;
    }

    bool SubpassRenderAttachmentLayout::operator!=(const SubpassRenderAttachmentLayout& other) const
    {
        return !(*this == other);
    }

    size_t RenderAttachmentLayout::GetHash() const
    {
        return eastl::hash<const RenderAttachmentLayout*>()(this);
    }

    bool RenderAttachmentLayout::operator==(const RenderAttachmentLayout& other) const
    {
        return IsEqual(other, true);
    }

    bool RenderAttachmentLayout::IsEqual(const RenderAttachmentLayout& other, const bool compareLoadStoreAction) const
    {
        if ((m_attachmentCount != other.m_attachmentCount) || (m_subpassCount != other.m_subpassCount))
        {
            return false;
        }

        for (uint32_t i = 0; i < m_attachmentCount; ++i)
        {
            if (m_attachmentFormats[i] != other.m_attachmentFormats[i])
            {
                return false;
            }
        }

        for (uint32_t i = 0; i < m_subpassCount; ++i)
        {
            if (!m_subpassLayouts[i].IsEqual(other.m_subpassLayouts[i], compareLoadStoreAction))
            {
                return false;
            }
        }

        return true;
    }

    size_t RenderAttachmentConfiguration::GetHash() const
    {
        size_t hash = m_renderAttachmentLayout.GetHash();
        eastl::hash_combine(hash, m_subpassIndex);
        return hash;
    }

    Format RenderAttachmentConfiguration::GetRenderTargetFormat(uint32_t index) const
    {
        const auto& subpassAttachmentLayout = m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex];
        return m_renderAttachmentLayout.m_attachmentFormats[subpassAttachmentLayout.m_rendertargetDescriptors[index].m_attachmentIndex];
    }

    Format RenderAttachmentConfiguration::GetSubpassInputFormat(uint32_t index) const
    {
        const auto& subpassAttachmentLayout = m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex];
        return m_renderAttachmentLayout.m_attachmentFormats[subpassAttachmentLayout.m_subpassInputDescriptors[index].m_attachmentIndex];
    }

    Format RenderAttachmentConfiguration::GetRenderTargetResolveFormat(uint32_t index) const
    {
        const auto& subpassAttachmentLayout = m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex];
        if (subpassAttachmentLayout.m_rendertargetDescriptors[index].m_resolveAttachmentIndex != InvalidRenderAttachmentIndex)
        {
            return m_renderAttachmentLayout.m_attachmentFormats[subpassAttachmentLayout.m_rendertargetDescriptors[index].m_resolveAttachmentIndex];
        }
        return Format::Unknown;
    }

    Format RenderAttachmentConfiguration::GetDepthStencilFormat() const
    {
        const auto& subpassAttachmentLayout = m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex];
        return subpassAttachmentLayout.m_depthStencilDescriptor.IsValid() ?
            m_renderAttachmentLayout.m_attachmentFormats[subpassAttachmentLayout.m_depthStencilDescriptor.m_attachmentIndex] :
            RHI::Format::Unknown;
    }

    uint32_t RenderAttachmentConfiguration::GetRenderTargetCount() const
    {
        return m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex].m_rendertargetCount;
    }

    uint32_t RenderAttachmentConfiguration::GetSubpassInputCount() const
    {
        return m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex].m_subpassInputCount;
    }

    bool RenderAttachmentConfiguration::DoesRenderTargetResolve(uint32_t index) const
    {
        return m_renderAttachmentLayout.m_subpassLayouts[m_subpassIndex].m_rendertargetDescriptors[index].m_resolveAttachmentIndex != InvalidRenderAttachmentIndex;
    }

    bool RenderAttachmentConfiguration::operator==(const RenderAttachmentConfiguration& other) const
    {
        return IsEqual(other, true);
    }

    bool RenderAttachmentConfiguration::IsEqual(const RenderAttachmentConfiguration& other, const bool compareLoadStoreAction) const
    {
        return m_renderAttachmentLayout.IsEqual(other.m_renderAttachmentLayout, compareLoadStoreAction) &&
            (m_subpassIndex == other.m_subpassIndex);
    }
}