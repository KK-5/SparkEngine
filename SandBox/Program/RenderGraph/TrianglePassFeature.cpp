#include "TrianglePassFeature.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/ShaderResource/ShaderResource.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/ShaderResource/ShaderResourceCompiler.h>

#include <Pass/PassContext.h>
#include <Pass/PassBuilder.h>
#include <RHI/Context/RHIContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>

namespace Spark::SandBox
{

TrianglePassFeature::TrianglePassFeature()
{
}

TrianglePassFeature::~TrianglePassFeature()
{
}

bool TrianglePassFeature::Init()
{
    // TODO: Create ViewSRG entity in RHIContext
    CreateViewSRG();

    // TODO: Create vertex/index buffers
    CreateVertexBuffer();

    // TODO: Create TrianglePass entity in PassExecuteContext::Current()
    CreateTrianglePass();

    TickBus::Handler::BusConnect();

    return true;
}

void TrianglePassFeature::Shutdown()
{
    TickBus::Handler::BusDisconnect();
}

void TrianglePassFeature::OnTick(float deltaTime)
{
    // TODO: Update MVP into ViewSRG, mark RHIUpdateTag
    UpdateViewSRG();
}

void TrianglePassFeature::CreateViewSRG()
{
    // TODO: Build ShaderResourceLayout (MVP constant + texture + sampler)
    // TODO: Create ShaderResource, init with layout
    // TODO: Create SRG entity in RHIContext with ImportedTag, ShaderResourceTag,
    //       ResourceName, ShaderResourceLayout, ShaderResource (owning),
    //       BackingShaderResource (raw)
}

void TrianglePassFeature::CreateTrianglePass()
{
    auto& passContext = *Spark::Render::PassExecuteContext::Current();

    // TODO: SPARK_RENDER_PASS(passContext, "TrianglePass")
    //   .Queue(Graphics)
    //   .VertexShader(m_vertShader).FragmentShader(m_fragShader)
    //   .RenderTargetLayout(...)
    //   .ShaderResource(0, m_viewSRGEntity)
    //   .Build([](auto& b){ /* Import swap chain as RTV */ })
    //   .Execute([this](ExecuteWork& work, RenderGraphExecuter&) {
    //       // Set viewport, bind vertex/index buffers, submit draw
    //   })
    //   .Finalize();
}

void TrianglePassFeature::CreateVertexBuffer()
{
    // TODO: Create vertex buffer (triangle vertices) and index buffer
}

void TrianglePassFeature::UpdateViewSRG()
{
    // TODO: Compute MVP matrix, call SetConstantRaw on m_srg, mark RHIUpdateTag
}

}
