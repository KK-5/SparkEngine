#include "Factory.h"

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/ConstantsLayout.h>

namespace Spark::RHI
{
    Ptr<ConstantsLayout> Factory::CreateConstantsLayout()
    {
        return new ConstantsLayout();
    }

}