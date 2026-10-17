#pragma once

#include <RHI/Pipeline/RenderStates.h>

#include "PipelineKey.h"
#include "PipelineVariant.h"

namespace Spark::Render
{
    //! What a pass's shader template says about the features it handles. Comes from the
    //! template itself once shaders declare their permutations; until then a template handles
    //! none.
    struct ShaderTemplateFeatures
    {
        //! The feature macros the template implements.
        FeatureMacro m_featureMacros         = FeatureMacro::None;
        //! The template's own code stands in for the material's (a depth template): the material
        //! shader comes in only for a feature that needs it.
        bool         m_ignoresMaterialShader = false;
    };

    //! The variant of `key` in a pass whose default render states are `passStates` and whose
    //! template is `shaderTemplate`. Static inputs only, so it is known before anything is drawn.
    PipelineVariant InterpretPipelineVariant(
        PipelineKey key, const RHI::RenderStates& passStates, const ShaderTemplateFeatures& shaderTemplate);
}
