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

namespace Spark::Render
{
    //! Release per-pass bindings before renderer-owned resource pools are shut down.
    //! PassContext can outlive those pools; the bindings retain their resource views.
    inline void ReleasePassBindings(PassContext& ctx)
    {
        for (auto [pass, comp] : ctx.GetView<PassBindings>().each())
        {
            comp.m_bindings = nullptr;
        }
    }
}
