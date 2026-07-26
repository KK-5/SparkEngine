#include "GBufferProcessor.h"

#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>

namespace Spark::Render
{
    void GBufferProcessor::Init()
    {
        // Create the per-pass material sampler SRG (space2, g_MatSampler) once, up front:
        // it is a constant (linear/wrap) and must exist before the OnTick bind loop, which
        // auto-injects it into this pass's DrawItems by PassTag. GBufferPass's DrawItems are
        // derived from world Drawables (OpaqueTag), so there is no per-frame work here.
        SetPassShaderSampler<SPARK_PASS_TAG("GBufferPass")>(
            2, RHI::InputName("g_MatSampler"),
            RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));
    }
}
