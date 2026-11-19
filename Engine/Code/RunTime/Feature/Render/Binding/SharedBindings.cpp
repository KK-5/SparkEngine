#include "SharedBindings.h"
#include "SharedGroupLayouts.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>

namespace Spark::Render
{
    Ptr<RHI::ShaderBindings> CreateSharedBindings(uint32_t spaceId)
    {
        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[SharedBindingPool] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[SharedBindingPool] RHI factory or device is null.");

        const auto* layout = FindSharedGroupLayout(*factory, spaceId);
        ASSERT(layout != nullptr, "[SharedBindingPool] No shared layout declares space {}.", spaceId);
        if (layout == nullptr)
        {
            return nullptr;
        }

        Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = spaceId;
        if (bindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[SharedBindingPool] ShaderBindings::Init failed for space {}.", spaceId);
            return nullptr;
        }
        return bindings;
    }
}
