#include "SharedBindings.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>

namespace Spark::Render
{
    Ptr<RHI::ShaderBindings> CreateSharedBindings(
        const RHI::ShaderInputList& inputs, uint32_t spaceId, const char* owner)
    {
        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[{}] RHI::RHIInterface service not registered.", owner);
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[{}] RHI factory or device is null.", owner);

        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(inputs);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = spaceId;
        if (bindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[{}] ShaderBindings::Init failed.", owner);
            return nullptr;
        }
        return bindings;
    }
}
