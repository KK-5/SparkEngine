/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/array.h>

#include <RHI/Pipeline/InputStreamLayout.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include "ShaderStages.h"
#include "ShaderStageFunction.h"
#include "RenderStates.h"
#include "PipelineLayoutDescriptor.h"
#include "RenderTargetLayout.h"

namespace Spark::RHI
{
    enum class PipelineStateType : uint32_t
    {
        Draw = 0,
        Dispatch,
        RayTracing,
        Count
    };

    const uint32_t PipelineStateTypeCount = static_cast<uint32_t>(PipelineStateType::Count);

    //! A base class to pipeline state descriptor.
    class PipelineStateDescriptor
    {
    public:
        virtual ~PipelineStateDescriptor() = default;

        //! Returns the derived pipeline state type.
        PipelineStateType GetType() const;

        //! Returns the hash of the pipeline state descriptor contents.
        size_t GetHash() const;

        bool operator == (const PipelineStateDescriptor& rhs) const;

        //! The pipeline layout describing the shader resource bindings.
        ConstPtr<PipelineLayoutDescriptor> m_pipelineLayoutDescriptor = nullptr;

        //! Values for specialization constants.
        // eastl::vector<SpecializationConstant> m_specializationData;

    protected:
        PipelineStateDescriptor(PipelineStateType pipelineStateType);

        virtual size_t GetHashInternal() const = 0;

    private:
        PipelineStateType m_type = PipelineStateType::Count;
    };

    //! Describes state necessary to build a compute pipeline state object. The compute pipe
    //! requires a pipeline layout and the shader byte code descriptor. Call Finalize after
    //! assigning data to build the hash value.
    //!
    //! NOTE: This class does not serialize. This is by design. the pipeline layout and shader
    //! byte code is likely shared by many PSOs and the serialization system would simply duplicate
    //! all of that data. However, the individual pieces are serializable, so a higher-level system
    //! could easily construct a PSO library.
    class PipelineStateDescriptorForDispatch final
        : public PipelineStateDescriptor
    {
    public:
        PipelineStateDescriptorForDispatch();

        /// Computes the hash value for this descriptor.
        size_t GetHashInternal() const override;

        bool operator == (const PipelineStateDescriptorForDispatch& rhs) const;

        /// The compute function containing byte code to compile.
        ConstPtr<ShaderStageFunction> m_computeFunction;
    };

    //! Describes state necessary to build a graphics pipeline state object (PSO). The graphics pipe
    //! requires a pipeline layout and the shader byte code descriptor, as well as the fixed-function
    //! input assembly stream layout, render target attachment layout, and various render states.
    //!
    //! NOTE: This class does not serialize by design. See PipelineStateDescriptorForDispatch for details.
    class PipelineStateDescriptorForDraw final
        : public PipelineStateDescriptor
    {
    public:
        PipelineStateDescriptorForDraw();

        /// Computes the hash value for this descriptor.
        size_t GetHashInternal() const override;

        bool operator == (const PipelineStateDescriptorForDraw& rhs) const;

        /// [Required] The vertex function to compile.
        ConstPtr<ShaderStageFunction> m_vertexFunction;

        /// [Optional] The geometry function to compile.
        ConstPtr<ShaderStageFunction> m_geometryFunction;

        /// [Required] The fragment function used to compile.
        ConstPtr<ShaderStageFunction> m_fragmentFunction;

        /// The input assembly vertex stream layout for the pipeline.
        InputStreamLayout m_inputStreamLayout;

        /// [Preferred] Dynamic-rendering render-target format layout for the pipeline.
        /// When non-empty (see RenderTargetLayout::IsEmpty), backends use this directly
        /// and ignore m_renderAttachmentConfiguration.
        RenderTargetLayout m_renderTargetLayout;

        /// [Legacy] Kept only for callers that still build through the subpass-era
        /// RenderAttachmentLayoutBuilder. Backends fall back to this when
        /// m_renderTargetLayout is empty. Remove once all call sites (examples, etc.)
        /// have migrated to m_renderTargetLayout.
        RenderAttachmentConfiguration m_renderAttachmentConfiguration;

        /// Various render states for the pipeline.
        RenderStates m_renderStates;
    };

    //! Describes state necessary to build a ray tracing pipeline state object. 
    class PipelineStateDescriptorForRayTracing final
        : public PipelineStateDescriptor
    {
    public:
        PipelineStateDescriptorForRayTracing();

        //! Computes the hash value for this descriptor.
        size_t GetHashInternal() const override;

        bool operator == (const PipelineStateDescriptorForRayTracing& rhs) const;

        // The ray tracing shader byte code
        ConstPtr<ShaderStageFunction> m_rayTracingFunction;
    };
}