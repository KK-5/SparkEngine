#include "MaterialUI.h"

#include <Material/Components.h>
#include <Reflection/TypeRegistry.h>
#include <Resource/AssetJsonSerializer.h>
#include <Resource/Material/StandardPBR.h>

namespace Editor
{
    using namespace Spark;

    bool MaterialExists(uint32_t handleId)
    {
        MetaType type = TypeRegistry::GetContext().Resolve<Resource::StandardPBR>();
        if (!type)
        {
            return false;
        }
        auto hasFn = type.func("HasComponent"_hs);
        if (!hasFn)
        {
            return false;
        }
        MetaAny r = hasFn.invoke({}, handleId);
        return r && r.cast<bool>();
    }

    bool TryGetMaterialAsset(uint32_t handleId, Resource::AssetId& out)
    {
        MetaType refType = TypeRegistry::GetContext().Resolve<Material::MaterialAssetRef>();
        if (!refType)
        {
            return false;
        }

        auto hasFn = refType.func("HasComponent"_hs);
        if (!hasFn)
        {
            return false;
        }
        MetaAny has = hasFn.invoke({}, handleId);
        if (!has || !has.cast<bool>())
        {
            return false;
        }

        MetaAny refPtr = refType.func("GetComponent"_hs).invoke({}, handleId);
        if (!refPtr)
        {
            return false;
        }

        MetaAny ref = *refPtr;
        if (const auto* typed = ref.try_cast<Material::MaterialAssetRef>())
        {
            out = typed->m_id;
            return true;
        }
        return false;
    }

    eastl::string MaterialIdentity(uint32_t handleId, bool exists)
    {
        if (!exists)
        {
            return "(none)";
        }

        Resource::AssetId id;
        if (TryGetMaterialAsset(handleId, id))
        {
            return Resource::AssetIdToDisplayString(id);
        }
        return "(scene material)";
    }
}
