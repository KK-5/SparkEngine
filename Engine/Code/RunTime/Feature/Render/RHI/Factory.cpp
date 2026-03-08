#include "Factory.h"

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderResource/ConstantsLayout.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>

namespace Spark::RHI
{
    Ptr<ConstantsLayout> Factory::CreateConstantsLayout()
    {
        return new ConstantsLayout();
    }

    Ptr<ShaderResourceLayout> Factory::CreateShaderResourceLayout()
    {
        return new ShaderResourceLayout();
    }
}