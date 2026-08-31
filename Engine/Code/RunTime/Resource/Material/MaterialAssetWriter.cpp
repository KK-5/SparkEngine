#include "MaterialAssetWriter.h"

#include <string>

#include <nlohmann/json.hpp>

#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>
#include <Serialization/Json.h>
#include <Serialization/JsonSerializer.h>

#include "MaterialAsset.h"
#include "MaterialFormat.h"

namespace Spark::Resource
{
    eastl::vector<uint8_t> WriteMaterialAsset(const MaterialAssetData& data)
    {
        ReflectContext& context    = TypeRegistry::GetContext();
        const MetaType  paramsType = context.Resolve<StandardPBR>();
        const MetaType  stateType  = context.Resolve<MaterialState>();
        if (!paramsType || !stateType)
        {
            LOG_ERROR("[MaterialAssetWriter] StandardPBR / MaterialState are not reflected; "
                      "is TypeRegistry::Register(Resource::Reflect) missing?");
            return {};
        }

        JsonValue state;
        JsonValue properties;
        if (!SerializeToJson(stateType.from_void(&data.GetState()), state)
            || !SerializeToJson(paramsType.from_void(&data.GetParams()), properties))
        {
            LOG_ERROR("[MaterialAssetWriter] A material field did not serialize.");
            return {};
        }

        // The shading model IS the parameter type's reflected name -- the same string the
        // compiler resolves back into a MetaType. Taking it off the type rather than
        // spelling a literal is what keeps the two ends from drifting.
        JsonValue root = JsonValue::object();
        root[kMaterialShadingModelKey] = paramsType.name();
        root[kMaterialStateKey]        = eastl::move(state);
        root[kMaterialPropertiesKey]   = eastl::move(properties);

        // Indented: a `.smat` is hand-written and hand-read at least as often as generated.
        const std::string text = root.dump(2);
        const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
        return eastl::vector<uint8_t>(bytes, bytes + text.size());
    }
}
