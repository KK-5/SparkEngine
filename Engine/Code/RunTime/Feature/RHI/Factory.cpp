#include "Factory.h"

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderResource/ConstantsLayout.h>

namespace Spark::RHI
{
    Ptr<ConstantsLayout> Factory::CreateConstantsLayout()
    {
        return new ConstantsLayout();
    }

}