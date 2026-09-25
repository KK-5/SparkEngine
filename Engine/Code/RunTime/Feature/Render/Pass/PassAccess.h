#pragma once

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/RHILimits.h>

#include <Pass/Pass.h>
#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>

#include <CoreComponents/Tags.h>
#include <EASTL/vector.h>

namespace Spark::Render
{
    //! Reap every per-pass ShaderBindings (created via CreatePassBindings)
    //! by its runtime PassShaderBindingsTag. Call once at pipeline / RenderSystem
    //! teardown — per-pass SRGs are persistent and have no external owner, so this is
    //! their single collection point.
    //!
    //! Destroyed here rather than tagged DeadTag like the view / instance SRGs: no tick
    //! follows teardown to reap DeadTag, so they would outlive RenderSystem, and their
    //! views hold images the render graph's pools own. Must run before
    //! RenderGraph::Shutdown releases those pools.
    inline void ReapPassShaderBindings(RHIContext& ctx)
    {
        eastl::vector<RHIHandle> dead;
        for (auto [entity, comp] : ctx.GetView<PassShaderBindingsTag, RHI::Components::ShaderBindings>().each())
        {
            dead.push_back(entity);
        }
        for (RHIHandle entity : dead)
        {
            ctx.DestoryEntity(entity);
        }
    }

    //! Mark a ShaderBindings entity as dirty so RenderGraphCompiler::CompileShaderInputs
    //! will recompile it before the next execute. Call after SetBuffer / SetImage /
    //! SetSampler / SetConstant changes. Idempotent: re-tagging a still-dirty
    //! entity within a single frame is a no-op.
    inline void MarkShaderBindingsUpdate(RHIContext& rhiCtx, RHIHandle entity)
    {
        if (entity == NullHandle)
        {
            return;
        }
        if (!rhiCtx.Has<RHI::ShaderBindingsUpdateTag>(entity))
        {
            rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(entity);
        }
    }
}
