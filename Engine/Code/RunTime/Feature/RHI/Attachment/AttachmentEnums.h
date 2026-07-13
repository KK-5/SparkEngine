/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Object/ObjectName.h>
#include <Math/Bit.h>
#include <RHI/Base.h>
#include <RHI/Resource/AccessFlags.h>

namespace Spark::RHI
{
    using AttachmentId = ObjectName;

    //! Describes how an attachment is accessed by a scope.
    enum class AttachmentAccess : uint32_t
    {
        Unknown = 0,
            
        //! The scope has read access to the attachment.
        Read      = BIT(0),

        //! The scope has write access to the attachment.
        Write     = BIT(1),

        //! The scope has read/write access to the attachment.
        ReadWrite = Read | Write
    };
    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::AttachmentAccess, uint32_t);

    //ATOM_RHI_REFLECT_API const char* ToString(AttachmentAccess attachmentAccess);

    //! Describes the underlying resource lifetime of an attachment with regards to the
    //! frame graph. Imported attachments are owned by the user and are persistent across
    //! frames. Transient attachments are owned by the transient attachment pool and are
    //! considered valid only for the current frame.
    enum class AttachmentLifetimeType : uint32_t
    {
        //! Imported by the user through the FrameGraph.
        Imported = 0,

        //! Created by the user for the current frame using the transient attachment FrameGraph.
        Transient
    };

    //! Describes how a Scope uses an Attachment
    enum class AttachmentUsage : uint32_t
    {
        //! Error value to catch uninitialized usage of this enum
        Uninitialized = 0,

        //! Render targets use the fixed-function output merger stage on the graphics queue.
        RenderTarget,

        //! A depth stencil attachment uses the fixed-function depth-stencil output merger stage on the graphics queue.
        DepthStencil,

        //! A shader attachment is exposed directly to the shader with either read or read-write access.
        Shader,

        //! A copy attachment is available for copy access via CopyItem.
        Copy,

        //! A resolve attachment target
        Resolve,

        //! An attachment used for predication
        Predication,

        //! An attachment used for indirect draw/dispatch.
        Indirect,

        //! An attachment that allows reading the output of a previous subpass.
        SubpassInput,

        //! An attachment used as Input Assembly in the scope. Only needed for buffers that are modified by the GPU (e.g Skinned Meshes), not
        //! for static data.
        InputAssembly,

        //! An attachment used for specifying the framebuffer shading rates.
        ShadingRate,

        //! An attachment used for presenting the swap chain.
        Present,

        //! Ray tracing acceleration structure (BLAS/TLAS). Distinct from generic Shader read in D3D12/Vulkan.
        RayTracingAccelerationStructure,

        Count,
    };

    enum class AttachmentUsageMask : uint32_t
    {
        None = 0,
        RenderTarget = BIT(static_cast<uint32_t>(AttachmentUsage::RenderTarget)),
        DepthStencil = BIT(static_cast<uint32_t>(AttachmentUsage::DepthStencil)),
        Shader = BIT(static_cast<uint32_t>(AttachmentUsage::Shader)),
        Copy = BIT(static_cast<uint32_t>(AttachmentUsage::Copy)),
        Resolve = BIT(static_cast<uint32_t>(AttachmentUsage::Resolve)),
        Predication = BIT(static_cast<uint32_t>(AttachmentUsage::Predication)),
        Indirect = BIT(static_cast<uint32_t>(AttachmentUsage::Indirect)),
        SubpassInput = BIT(static_cast<uint32_t>(AttachmentUsage::SubpassInput)),
        InputAssembly = BIT(static_cast<uint32_t>(AttachmentUsage::InputAssembly)),
        ShadingRate = BIT(static_cast<uint32_t>(AttachmentUsage::ShadingRate)),
        Present = BIT(static_cast<uint32_t>(AttachmentUsage::Present)),
        RayTracingAccelerationStructure = BIT(static_cast<uint32_t>(AttachmentUsage::RayTracingAccelerationStructure)),
        All =
            RenderTarget | DepthStencil | Shader | Copy | Resolve | Predication | Indirect | SubpassInput | InputAssembly | ShadingRate |
            Present | RayTracingAccelerationStructure
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::AttachmentUsageMask, uint32_t);

    //ATOM_RHI_REFLECT_API const char* ToString(AttachmentUsage attachmentUsage);

    //! Describes in which pipeline stages a Scope Attachment is used
    enum class AttachmentStage : uint32_t
    {
        //! Error value to catch uninitialized usage of this enum
        Uninitialized = 0,

        //! Vertex shader stage
        VertexShader = BIT(0),

        //! Fragment shader stage
        FragmentShader = BIT(1),

        //! Compute shader stage
        ComputeShader = BIT(2),

        //! Ray tracing shader stage
        RayTracingShader = BIT(3),

        //! Early depth/stencil test stage
        EarlyFragmentTest = BIT(4),

        //! Late depth/stencil test stage
        LateFragmentTest = BIT(5),

        //! Color attachment output stage
        ColorAttachmentOutput = BIT(6),

        //! Transfer stage
        Copy = BIT(7),

        //! Conditional rendering stage
        Predication = BIT(8),

        //! Indirect draw stage
        DrawIndirect = BIT(9),

        //! Vertex input stage (when vertex data is fetch from the inputs)
        //! Runs before the Vertex Shader stage
        VertexInput = BIT(10),

        //! Variable shading rate stage
        ShadingRate = BIT(11),

        //! All graphics stages
        AnyGraphics = VertexShader | FragmentShader | ComputeShader | RayTracingShader,

        //! All stages
        Any = AnyGraphics | EarlyFragmentTest | LateFragmentTest | ColorAttachmentOutput | Copy | Predication | DrawIndirect |
            VertexInput | ShadingRate
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::AttachmentStage, uint32_t);

    //! Translate a render-layer (usage, access) attachment description into the backend-agnostic
    //! AccessFlags bitmask consumed by ResourceState / barriers. Folds in the usage-based
    //! normalization (RenderTarget/DepthStencil ReadWrite->Write, Shader Write->ReadWrite UAV).
    //! Buffer and Image are split: a Shader read differs (buffers add ConstantBufferRead for
    //! CBV/structured; images are SRV-only) and each rejects the other's usages.
    AccessFlags ConvertBufferAccess(AttachmentUsage usage, AttachmentAccess access);
    AccessFlags ConvertImageAccess(AttachmentUsage usage, AttachmentAccess access);

    //! Describes the action the hardware should use when loading an attachment prior to a scope.
    enum class AttachmentLoadAction : uint8_t
    {
        //! The attachment contents should be preserved (loaded from memory).
        Load = 0,

        //! The attachment contents should be cleared (using the provided clear value).
        Clear,

        //! The attachment contents are undefined. Use when writing to entire contents of view.
        DontCare,

        //! The attachment contents will be undefined inside the current scope and the resource is not accessed.
        //! Will fallback to a Load op if the platform doesn't support it.
        None
    };

    //! Describes the action the hardware should use when storing an attachment after a scope.
    enum class AttachmentStoreAction : uint8_t
    {
        //! The attachment contents must be preserved after the current scope.
        Store = 0,

        //! The attachment contents can be undefined after the current scope.
        DontCare,

        //! The attachment contents are read only. This avoid any write back operations.
        //! If values are written this behaves identically to the DontCare op.
        //! Will fallback to a Store op if the platform doesn't support it.
        None
    };

    //! Describes the type of data the attachment represents
    enum class AttachmentType : uint8_t
    {
        //! The attachment is an image.
        Image = 0,

        //! The attachment is a buffer.
        Buffer,

        //! The attachment is a resolve (for example resolving an MSAA texture)
        Resolve,

        //! Error value to catch uninitialized usage of this enum
        Uninitialized,
    };

    // ATOM_RHI_REFLECT_API const char* ToString(AZ::RHI::AttachmentType attachmentType);

    //! Describes the type of scope attachment for a QueryPool
    enum class QueryPoolScopeAttachmentType : uint8_t
    {
        Local, // The results of the queries will be use by another scope in the graph.
        Global // The results of the queries will be accessed in subsequent frames.
    };

    //! Describes the type of support for Subpass inputs.
    enum class SubpassInputSupportType : uint32_t
    {
        None = 0,
        Color = BIT(0),          // Subpass inputs for color attachments is supported.
        DepthStencil = BIT(1),    // Subpass inputs for depth/stencil attachment is supported.
        All = Color | DepthStencil
    };
    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::SubpassInputSupportType, uint32_t)
}
