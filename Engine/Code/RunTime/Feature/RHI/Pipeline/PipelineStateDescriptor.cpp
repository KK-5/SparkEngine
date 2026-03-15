/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineStateDescriptor.h"

#include <Log/SpdLogSystem.h>
#include <EASTLEX/hash.h>

namespace Spark::RHI
{
    PipelineStateDescriptor::PipelineStateDescriptor(PipelineStateType pipelineStateType)
        : m_type{pipelineStateType}
    {}

    PipelineStateType PipelineStateDescriptor::GetType() const
    {
        return m_type;
    }

    size_t PipelineStateDescriptor::GetHash() const
    {
        ASSERT(m_pipelineLayoutDescriptor, "Pipeline layout descriptor is null.");
        size_t seed =  0 ;
        eastl::hash_combine_raw(seed, m_pipelineLayoutDescriptor->GetHash());
        eastl::hash_combine_raw(seed, GetHashInternal());
        return seed;
    }

    bool PipelineStateDescriptor::operator==(const PipelineStateDescriptor& rhs) const
    {
        //return m_type == rhs.m_type && m_specializationData == rhs.m_specializationData;
        return m_type == rhs.m_type;
    }

    PipelineStateDescriptorForDraw::PipelineStateDescriptorForDraw()
        : PipelineStateDescriptor(PipelineStateType::Draw)
    {}

    PipelineStateDescriptorForDispatch::PipelineStateDescriptorForDispatch()
        : PipelineStateDescriptor(PipelineStateType::Dispatch)
    {}

    PipelineStateDescriptorForRayTracing::PipelineStateDescriptorForRayTracing()
        : PipelineStateDescriptor(PipelineStateType::RayTracing)
    {}

    size_t PipelineStateDescriptorForDispatch::GetHashInternal() const
    {
        ASSERT(m_computeFunction, "Compute function is null.");

        size_t seed{ 0 };
        eastl::hash_combine_raw(seed, m_computeFunction->GetHash());
        return seed;
    }

    size_t PipelineStateDescriptorForDraw::GetHashInternal() const
    {
        size_t seed{ 0 };

        if (m_vertexFunction)
        {
            eastl::hash_combine_raw(seed, m_vertexFunction->GetHash());
        }
        if (m_geometryFunction)
        {
            eastl::hash_combine_raw(seed, m_geometryFunction->GetHash());
        }
        if (m_fragmentFunction)
        {
            eastl::hash_combine_raw(seed, m_fragmentFunction->GetHash());
        }

        eastl::hash_combine_raw(seed, m_inputStreamLayout.GetHash());
        eastl::hash_combine_raw(seed, m_renderAttachmentConfiguration.GetHash());

        seed = m_renderStates.GetHash(seed);

        return seed;
    }

    size_t PipelineStateDescriptorForRayTracing::GetHashInternal() const
    {
        size_t seed{ 0 };
        eastl::hash_combine_raw(seed, m_rayTracingFunction->GetHash());
        return seed;
    }

    bool PipelineStateDescriptorForDraw::operator == (const PipelineStateDescriptorForDraw& rhs) const
    {
        return m_fragmentFunction == rhs.m_fragmentFunction && m_pipelineLayoutDescriptor == rhs.m_pipelineLayoutDescriptor &&
            m_renderStates == rhs.m_renderStates && m_vertexFunction == rhs.m_vertexFunction &&
            m_geometryFunction == rhs.m_geometryFunction && m_inputStreamLayout == rhs.m_inputStreamLayout && 
            m_renderAttachmentConfiguration == rhs.m_renderAttachmentConfiguration/* && m_specializationData == rhs.m_specializationData */;
    }

    bool PipelineStateDescriptorForDispatch::operator == (const PipelineStateDescriptorForDispatch& rhs) const
    {
        return m_computeFunction == rhs.m_computeFunction &&
            m_pipelineLayoutDescriptor == rhs.m_pipelineLayoutDescriptor/* &&
            m_specializationData == rhs.m_specializationData */;
    }

    bool PipelineStateDescriptorForRayTracing::operator == (const PipelineStateDescriptorForRayTracing& rhs) const
    {
        return m_pipelineLayoutDescriptor == rhs.m_pipelineLayoutDescriptor &&
            m_rayTracingFunction == rhs.m_rayTracingFunction/* &&
            m_specializationData == rhs.m_specializationData*/;
    }
}