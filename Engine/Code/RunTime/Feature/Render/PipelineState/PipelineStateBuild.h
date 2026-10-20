#pragma once

#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/RenderStates.h>

#include <Pass/Component/PassComponents.h>

#include "PipelineVariant.h"

namespace Spark::Render
{
    //! The pass's render states with what `variant` overrides.
    RHI::RenderStates ApplyPipelineVariant(const RHI::RenderStates& passStates, const PipelineVariant& variant);

    //! The draw PSO description of a pass, with `variant` applied to its default. Refers to the
    //! pass's stage functions and layout, so it makes nothing. `passName` only names the pass
    //! in asserts.
    RHI::PipelineStateDescriptorForDraw BuildDrawPipelineDescriptor(
        const PassStageFunctions& stageFunctions,
        const PassPipelineState&  pipelineState,
        const PassPipelineLayout& layout,
        const PipelineVariant&    variant,
        const char*               passName);
}
