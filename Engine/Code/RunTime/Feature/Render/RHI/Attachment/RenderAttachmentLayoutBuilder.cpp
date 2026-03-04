/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "RenderAttachmentLayoutBuilder.h"

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    RenderAttachmentLayoutBuilder::RenderAttachmentLayoutBuilder()
    {
        Reset();
    }

    ResultCode RenderAttachmentLayoutBuilder::End(
        RenderAttachmentLayout& builtRenderAttachmentLayout,
        eastl::array<ObjectName, Limits::Pipeline::RenderAttachmentCountMax>* attachmentNames)
    {
        auto& renderAttachmentFormats = builtRenderAttachmentLayout.m_attachmentFormats;
        Format depthStencilFormat = Format::Unknown;
        eastl::unordered_map<ObjectName, uint32_t> renderAttachmentsMap;
        // This lambda will handle if the render attachment needs to resolve and will add a resolve attachment if needed.
        auto handleResolveAttachment = [&](const SubpassAttachmentLayoutBuilder::RenderAttachmentEntry& attachment, uint32_t& resolveAttachmentIndex)
        {
            resolveAttachmentIndex = InvalidRenderAttachmentIndex;
            if (!attachment.m_resolveName.IsEmpty())
            {
                AttachmentLoadStoreAction loadStoreAction;
                loadStoreAction.m_loadAction = AttachmentLoadAction::DontCare;
                loadStoreAction.m_storeAction = AttachmentStoreAction::Store;
                auto findResolveIter = renderAttachmentsMap.find(attachment.m_resolveName);
                if (findResolveIter == renderAttachmentsMap.end())
                {
                    if (attachment.m_format == Format::Unknown)
                    {
                        ASSERT(false, "Invalid format for resolve attachment {}", attachment.m_name.GetCStr());
                        return ResultCode::InvalidArgument;
                    }
                    resolveAttachmentIndex = builtRenderAttachmentLayout.m_attachmentCount++;
                    renderAttachmentsMap[attachment.m_resolveName] = resolveAttachmentIndex;
                    renderAttachmentFormats[resolveAttachmentIndex] = attachment.m_format;
                }
                else
                {
                    resolveAttachmentIndex = findResolveIter->second;
                }

                if (attachment.m_format != Format::Unknown && renderAttachmentFormats[resolveAttachmentIndex] != attachment.m_format)
                {
                    ASSERT(false, "Incompatible format for resolve attachment {}. Expected {}. Actual {}", attachment.m_name.GetCStr(), 
                        static_cast<uint32_t>(renderAttachmentFormats[resolveAttachmentIndex]), 
                        static_cast<uint32_t>(attachment.m_format));
                    return ResultCode::InvalidArgument;
                }
            }
            return ResultCode::Success;
        };

        for (const auto& builder : m_subpassLayoutBuilders)
        {
            SubpassRenderAttachmentLayout& subpassLayout = builtRenderAttachmentLayout.m_subpassLayouts[builtRenderAttachmentLayout.m_subpassCount++];
            subpassLayout.m_rendertargetCount = static_cast<uint32_t>(builder.m_renderTargetAttachments.size());
            subpassLayout.m_subpassInputCount = static_cast<uint32_t>(builder.m_subpassInputAttachments.size());

            // First add the resolve attachments, so they can be found when adding the MS attachments.
            for (uint32_t i = 0; i < builder.m_renderTargetAttachments.size(); ++i)
            {
                const SubpassAttachmentLayoutBuilder::RenderAttachmentEntry& renderTargetAttachment = builder.m_renderTargetAttachments[i];
                uint32_t resolveAttachmentIndex;
                handleResolveAttachment(renderTargetAttachment, resolveAttachmentIndex);
            }

            uint32_t resolveAttachmentIndex;
            ResultCode result = handleResolveAttachment(builder.m_depthStencilAttachment, resolveAttachmentIndex);

            for (uint32_t i = 0; i < builder.m_renderTargetAttachments.size(); ++i)
            {
                const SubpassAttachmentLayoutBuilder::RenderAttachmentEntry& renderTargetAttachment = builder.m_renderTargetAttachments[i];
                uint32_t attachmentIndex = 0;
                // First look if the render target has already been added to the list of attachments.
                auto findIter = renderAttachmentsMap.find(renderTargetAttachment.m_name);
                if (findIter == renderAttachmentsMap.end())
                {
                    if (renderTargetAttachment.m_format == Format::Unknown)
                    {
                        ASSERT(false, "Invalid format for rendertarget %s", renderTargetAttachment.m_name.GetCStr());
                        return ResultCode::InvalidArgument;
                    }
                    attachmentIndex = builtRenderAttachmentLayout.m_attachmentCount++;
                    renderAttachmentsMap[renderTargetAttachment.m_name] = attachmentIndex;
                    renderAttachmentFormats[attachmentIndex] = renderTargetAttachment.m_format;
                }
                else
                {
                    attachmentIndex = findIter->second;
                }

                // Add the use of the attachment to the subpass.
                result = handleResolveAttachment(renderTargetAttachment, resolveAttachmentIndex);
                if (result != ResultCode::Success)
                {
                    return result;
                }

                if (renderTargetAttachment.m_format != Format::Unknown && renderAttachmentFormats[attachmentIndex] != renderTargetAttachment.m_format)
                {
                    ASSERT(false, "Incompatible format for attachment {}. Expected {}. Actual {}", 
                        renderTargetAttachment.m_name.GetCStr(), 
                        static_cast<uint32_t>(renderAttachmentFormats[attachmentIndex]), 
                        static_cast<uint32_t>(renderTargetAttachment.m_format));
                    return ResultCode::InvalidArgument;
                }

                subpassLayout.m_rendertargetDescriptors[i] = RenderAttachmentDescriptor{ attachmentIndex, resolveAttachmentIndex,
                                                                                         renderTargetAttachment.m_loadStoreAction,
                                                                                         renderTargetAttachment.m_scopeAttachmentAccess,
                                                                                         renderTargetAttachment.m_scopeAttachmentStage,
                                                                                         renderTargetAttachment.m_extras};
            }

            if (!builder.m_depthStencilAttachment.m_name.IsEmpty())
            {
                // Check if the depth/stencil has already been added and if it has, check if the format is the same.
                if (depthStencilFormat != Format::Unknown &&
                    builder.m_depthStencilAttachment.m_format != Format::Unknown &&
                    depthStencilFormat != builder.m_depthStencilAttachment.m_format)
                {
                    ASSERT(false, "Invalid depth stencil format.");
                    return ResultCode::InvalidArgument;
                }

                // Search for the depth/stencil attachment in the list of added attachments.
                uint32_t attachmentIndex = 0;
                auto findIter = renderAttachmentsMap.find(builder.m_depthStencilAttachment.m_name);
                if (findIter == renderAttachmentsMap.end())
                {
                    if (builder.m_depthStencilAttachment.m_format == Format::Unknown)
                    {
                        ASSERT(false, "Invalid depth stencil format.");
                        return ResultCode::InvalidArgument;
                    }

                    depthStencilFormat = builder.m_depthStencilAttachment.m_format;
                    attachmentIndex = builtRenderAttachmentLayout.m_attachmentCount++;
                    renderAttachmentsMap[builder.m_depthStencilAttachment.m_name] = attachmentIndex;
                    renderAttachmentFormats[attachmentIndex] = depthStencilFormat;
                }
                else
                {
                    attachmentIndex = findIter->second;
                }

                result = handleResolveAttachment(builder.m_depthStencilAttachment, resolveAttachmentIndex);
                if (result != ResultCode::Success)
                {
                    return result;
                }
                subpassLayout.m_depthStencilDescriptor = RenderAttachmentDescriptor{ attachmentIndex, resolveAttachmentIndex,
                                                builder.m_depthStencilAttachment.m_loadStoreAction,
                                                builder.m_depthStencilAttachment.m_scopeAttachmentAccess,
                                                builder.m_depthStencilAttachment.m_scopeAttachmentStage,
                                                builder.m_depthStencilAttachment.m_extras};
            }

            // Add the subpass inputs.
            for (uint32_t i = 0; i < builder.m_subpassInputAttachments.size(); ++i)
            {
                auto& subpassInputAttachmentName = builder.m_subpassInputAttachments[i].m_name;
                uint32_t attachmentIndex = 0;
                auto findIter = renderAttachmentsMap.find(subpassInputAttachmentName);
                if (findIter == renderAttachmentsMap.end())
                {
                    ASSERT(false, "Could not find subpassInput {}", subpassInputAttachmentName.GetCStr());
                    return ResultCode::InvalidArgument;
                }

                attachmentIndex += findIter->second;
                subpassLayout.m_subpassInputDescriptors[i] =
                    SubpassInputDescriptor{ attachmentIndex,
                                            builder.m_subpassInputAttachments[i].m_imageAspects,
                                            builder.m_subpassInputAttachments[i].m_scopeAttachmentAccess,
                                            builder.m_subpassInputAttachments[i].m_scopeAttachmentStage,
                                            builder.m_subpassInputAttachments[i].m_loadStoreAction,
                                            builder.m_subpassInputAttachments[i].m_extras};
            }

            // Add the shading rate attachment.
            if (!builder.m_shadingRateAttachment.m_name.IsEmpty())
            {
                uint32_t attachmentIndex = 0;
                auto findIter = renderAttachmentsMap.find(builder.m_shadingRateAttachment.m_name);
                if (findIter == renderAttachmentsMap.end())
                {
                    if (builder.m_shadingRateAttachment.m_format == Format::Unknown)
                    {
                        ASSERT(false, "Invalid shading rate format {}");
                        return ResultCode::InvalidArgument;
                    }

                    attachmentIndex = builtRenderAttachmentLayout.m_attachmentCount++;
                    renderAttachmentsMap[builder.m_shadingRateAttachment.m_name] = attachmentIndex;
                    renderAttachmentFormats[attachmentIndex] = builder.m_shadingRateAttachment.m_format;
                }
                else
                {
                    attachmentIndex = findIter->second;
                }

                subpassLayout.m_shadingRateDescriptor = RenderAttachmentDescriptor{ attachmentIndex,
                                                                                    InvalidRenderAttachmentIndex,
                                                                                    builder.m_shadingRateAttachment.m_loadStoreAction,
                                                                                    builder.m_shadingRateAttachment.m_scopeAttachmentAccess,
                                                                                    builder.m_shadingRateAttachment.m_scopeAttachmentStage,
                                                                                    builder.m_shadingRateAttachment.m_extras};
            }
        }

        if (attachmentNames)
        {
            auto& namesArray = *attachmentNames;
            for (const auto& attachment : renderAttachmentsMap)
            {
                namesArray[attachment.second] = attachment.first;
            }
        }

        return ResultCode::Success;
    }

    void RenderAttachmentLayoutBuilder::Reset()
    {
        m_subpassLayoutBuilders.clear();
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::AddSubpass()
    {
        m_subpassLayoutBuilders.push_back(SubpassAttachmentLayoutBuilder(static_cast<uint32_t>(m_subpassLayoutBuilders.size())));
        auto& newLayoutBuilder = m_subpassLayoutBuilders.back();
        return &newLayoutBuilder;
    }

    uint32_t RenderAttachmentLayoutBuilder::GetSubpassCount() const
    {
        return static_cast<uint32_t>(m_subpassLayoutBuilders.size());
    }
        
    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder(uint32_t subpassIndex)
        : m_subpassIndex(subpassIndex)
    {}

    uint32_t RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::GetSubpassIndex() const
    {
        return m_subpassIndex;
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::RenderTargetAttachment(
        Format format,
        const ObjectName& name /*= {}*/,
        const AttachmentLoadStoreAction& loadStoreAction /*= AttachmentLoadStoreAction()*/,            
        bool resolve /*= false*/,
        RenderAttachmentExtras* extras /*= nullptr*/)
    {
       ObjectName attachmentName = name;
        if (attachmentName.IsEmpty())
        {
            // Assign a temp name if it's empty.
            attachmentName = spdlog::fmt_lib::format("Color{}_Subpass{}", m_renderTargetAttachments.size(), m_subpassIndex).c_str();
        }

        m_renderTargetAttachments.push_back({ attachmentName,
                                              format,
                                              loadStoreAction,
                                              {},
                                              RHI::ScopeAttachmentAccess::Write,
                                              RHI::ScopeAttachmentStage::ColorAttachmentOutput,
                                              extras });
        if (resolve)
        {
            return ResolveAttachment(attachmentName);
        }
        return this;
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::RenderTargetAttachment(
        Format format,
        bool resolve)
    {
        return RenderTargetAttachment(format, {}, AttachmentLoadStoreAction(), resolve);
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::RenderTargetAttachment(
        const ObjectName& name,
        bool resolve)
    {
        return RenderTargetAttachment(
            Format::Unknown, name, AttachmentLoadStoreAction(), resolve);
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::RenderTargetAttachment(
        const ObjectName& name,
        const AttachmentLoadStoreAction& loadStoreAction /*= AttachmentLoadStoreAction()*/,
        bool resolve /*= false*/,
        RenderAttachmentExtras* extras /*= nullptr*/)
    {
        return RenderTargetAttachment(Format::Unknown, name, loadStoreAction, resolve, extras);
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::DepthStencilAttachment(
        const ObjectName name /*= {}*/,
        const AttachmentLoadStoreAction& loadStoreAction /*= AttachmentLoadStoreAction()*/,
        RHI::ScopeAttachmentAccess scopeAttachmentAccess,
        RHI::ScopeAttachmentStage scopeAttachmentStage,
        RenderAttachmentExtras* extras /*= nullptr*/)
    {
        return DepthStencilAttachment(
            m_depthStencilAttachment.m_format, name, loadStoreAction, scopeAttachmentAccess, scopeAttachmentStage, extras);
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::DepthStencilAttachment(
        const AttachmentLoadStoreAction& loadStoreAction,
        RHI::ScopeAttachmentAccess scopeAttachmentAccess,
        RHI::ScopeAttachmentStage scopeAttachmentStage,
        RenderAttachmentExtras* extras /*= nullptr*/)
    {
        return DepthStencilAttachment(
            m_depthStencilAttachment.m_format, {}, loadStoreAction, scopeAttachmentAccess, scopeAttachmentStage, extras);
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::SubpassInputAttachment(
        const ObjectName& name,
        RHI::ImageAspectFlags aspectFlags,
        const AttachmentLoadStoreAction& loadStoreAction /*= AttachmentLoadStoreAction()*/,
        RenderAttachmentExtras* extras /*= nullptr*/)
    {
        m_subpassInputAttachments.push_back(SubpassAttachmentEntry{ name,
                                                                    aspectFlags,
                                                                    RHI::ScopeAttachmentAccess::Read,
                                                                    RHI::ScopeAttachmentStage::FragmentShader,
                                                                    loadStoreAction,
                                                                    extras });
        return this;
    }

    RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder* RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::ShadingRateAttachment(
        Format format,
        const ObjectName& name /*= {}*/,
        RenderAttachmentExtras* extras /*= nullptr*/)
    {
        ASSERT(
            m_shadingRateAttachment.m_format == Format::Unknown || format == m_shadingRateAttachment.m_format,
            "Shading Rate format has already been set");
        // Assign a temp name if it's empty.
        m_shadingRateAttachment = RenderAttachmentEntry{
            name.IsEmpty() ? ObjectName(eastl::string("ShadingRate_Subpass") + eastl::to_string(m_subpassIndex)) : name,
            format,
            AttachmentLoadStoreAction(),
            {},
            RHI::ScopeAttachmentAccess::Read,
            RHI::ScopeAttachmentStage::ShadingRate,
            extras
        };
        m_shadingRateAttachment.m_loadStoreAction.m_storeAction = AttachmentStoreAction::None;
        return this;
    }

    bool RenderAttachmentLayoutBuilder::SubpassAttachmentLayoutBuilder::HasAttachments() const
    {
        return !m_subpassInputAttachments.empty() || !m_renderTargetAttachments.empty() || !m_shadingRateAttachment.m_name.IsEmpty() ||
            !m_depthStencilAttachment.m_name.IsEmpty();
    }
}