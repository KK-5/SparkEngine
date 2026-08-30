#include "MaterialAssetCompiler.h"

#include <string>

#include <nlohmann/json.hpp>

#include <HashString/HashString.h>
#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>
#include <Serialization/Json.h>
#include <Serialization/JsonSerializer.h>

#include "MaterialAsset.h"
#include "MaterialRawTypes.h"

namespace Spark::Resource
{
    namespace
    {
        //! The three top-level keys are written and read by hand, lowerCamel like `path` /
        //! `sub` / `desc`. What lives INSIDE state and properties are reflected names, and
        //! those are handed back to the serializer -- an operation may pass its sub-parts
        //! to the dispatcher, just not itself.
        constexpr const char* kShadingModelKey = "shadingModel";
        constexpr const char* kStateKey        = "state";
        constexpr const char* kPropertiesKey   = "properties";

        //! The shading model names the type that interprets `properties`, and that name is
        //! the type's reflected name -- so validating it IS resolving it, and there is no
        //! enum or name table to keep in step.
        //!
        //! An absent key is the default model ("a missing key is its default"). A key that
        //! names something else is an error rather than a fall back to StandardPBR:
        //! interpreting another model's properties as StandardPBR's would pick up the names
        //! that happen to match and drop the rest, yielding a plausible wrong material out
        //! of a file we cannot re-issue.
        bool ShadingModelIsSupported(const JsonValue& root, const AssetId& id)
        {
            const auto entry = root.find(kShadingModelKey);
            if (entry == root.end())
            {
                return true;
            }

            if (!entry->is_string())
            {
                LOG_ERROR("[MaterialAssetCompiler] '{}': {} must be a string, got {}.",
                    id.GetPath().c_str(), kShadingModelKey, entry->type_name());
                return false;
            }

            ReflectContext& context = TypeRegistry::GetContext();
            const std::string name  = entry->get<std::string>();
            const MetaType    model = context.Resolve(HashString::value(name.c_str(), name.size()));

            if (!model)
            {
                LOG_ERROR("[MaterialAssetCompiler] '{}' declares shading model '{}', which "
                          "names no reflected type.", id.GetPath().c_str(), name.c_str());
                return false;
            }
            if (model != context.Resolve<StandardPBR>())
            {
                LOG_ERROR("[MaterialAssetCompiler] '{}' declares shading model '{}', which "
                          "this build cannot express.", id.GetPath().c_str(), name.c_str());
                return false;
            }
            return true;
        }

        //! An absent section is every field at its default; a present one must be an object
        //! and must read cleanly. Unknown keys inside it are ignored, which is the format's
        //! version rule -- and the reason a misspelled property in a hand-written `.smat`
        //! goes silently to its default rather than reporting anything.
        bool ReadSection(const JsonValue& root, const char* key, MetaAny& target, const AssetId& id)
        {
            const auto entry = root.find(key);
            if (entry == root.end())
            {
                return true;
            }

            if (!entry->is_object())
            {
                LOG_ERROR("[MaterialAssetCompiler] '{}': {} must be an object, got {}.",
                    id.GetPath().c_str(), key, entry->type_name());
                return false;
            }

            if (!DeserializeFromJson(*entry, target))
            {
                LOG_ERROR("[MaterialAssetCompiler] '{}': {} did not read cleanly.",
                    id.GetPath().c_str(), key);
                return false;
            }
            return true;
        }
    }

    UniquePtr<AssetData> MaterialAssetCompiler::Compile(
        const AssetId& id, const MaterialRawData& raw) const
    {
        // Already the values -- a parent pulled this out of its own file. Nothing to parse.
        if (raw.GetKind() == MaterialRawData::Kind::Decoded)
        {
            const auto& decoded = static_cast<const MaterialDecodedRawData&>(raw);
            auto data = MakeUnique<MaterialAssetData>();
            data->m_params = decoded.GetParams();
            data->m_state  = decoded.GetState();
            return data;
        }

        const eastl::vector<uint8_t>& bytes =
            static_cast<const MaterialEncodedRawData&>(raw).GetBytes();
        if (bytes.empty())
        {
            LOG_ERROR("[MaterialAssetCompiler] '{}' is empty.", id.GetPath().c_str());
            return nullptr;
        }

        const JsonValue root = JsonValue::parse(bytes.data(), bytes.data() + bytes.size(),
                                                nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            LOG_ERROR("[MaterialAssetCompiler] '{}' is not a JSON object.", id.GetPath().c_str());
            return nullptr;
        }

        if (!ShadingModelIsSupported(root, id))
        {
            return nullptr;
        }

        ReflectContext& context = TypeRegistry::GetContext();
        auto data = MakeUnique<MaterialAssetData>();

        // Named locals, not temporaries handed to ReadSection: from_void yields a
        // non-owning any, but a target that ever became an owning one would take the
        // writes onto a copy and lose them without a word.
        MetaAny state      = context.Resolve<MaterialState>().from_void(&data->m_state);
        MetaAny properties = context.Resolve<StandardPBR>().from_void(&data->m_params);

        if (!ReadSection(root, kStateKey, state, id)
            || !ReadSection(root, kPropertiesKey, properties, id))
        {
            return nullptr;
        }

        return data;
    }
}
