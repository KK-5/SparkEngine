#include "TypeRegistry.h"

namespace Spark
{
    ReflectContext TypeRegistry::m_reflectContext {};
    eastl::vector<TypeRegistry::RegisterFunc> TypeRegistry::m_funcs;
}