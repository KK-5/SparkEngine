#include "AssetJsonSerializer.h"

#include <string>

#include <nlohmann/json.hpp>

#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>
#include <Serialization/JsonSerializer.h>

#include "Image/ImageAsset.h"
#include "Material/MaterialAsset.h"
#include "Model/ModelAsset.h"
#include "Shader/ShaderAsset.h"

namespace Spark::Resource
{
    namespace
    {
        eastl::string ReadString(const JsonValue& in, const char* key)
        {
            const auto entry = in.find(key);
            if (entry == in.end() || !entry->is_string())
            {
                return {};
            }
            const std::string text = entry->get<std::string>();
            return eastl::string(text.c_str(), text.size());
        }
    }

    bool AssetIdToJson(const AssetId& id, JsonValue& out)
    {
        if (!id.IsValid())
        {
            LOG_ERROR("[AssetJsonSerializer] Cannot write an id that names no asset.");
            return false;
        }

        // Written key by key rather than walked off reflected fields: handing the whole id
        // to SerializeToJson would land back on AssetId's own JsonOperation. AssetType is a
        // sub-part, so it still goes through the dispatcher and keeps its reflected name.
        const MetaType typeMeta = TypeRegistry::GetContext().Resolve<AssetType>();
        const AssetType type = id.GetAssetType();
        JsonValue typeJson;
        if (!typeMeta || !SerializeToJson(typeMeta.from_void(&type), typeJson))
        {
            LOG_ERROR("[AssetJsonSerializer] AssetType did not serialize; is "
                      "TypeRegistry::Register(Resource::Reflect) missing?");
            return false;
        }

        const eastl::string path = id.GetPath();
        out = JsonValue::object();
        out["type"] = eastl::move(typeJson);
        out["path"] = std::string(path.c_str(), path.size());

        if (id.IsSubAsset())
        {
            const eastl::string sub = id.GetSubLabel();
            out["sub"] = std::string(sub.c_str(), sub.size());
        }

        JsonValue descriptor;
        if (id.GetDescriptor() != nullptr
            && DescriptorToJson(*id.GetDescriptor(), id.GetAssetType(), descriptor))
        {
            out["desc"] = eastl::move(descriptor);
        }
        return true;
    }

    AssetId AssetIdFromJson(const JsonValue& in)
    {
        if (!in.is_object())
        {
            LOG_ERROR("[AssetJsonSerializer] Expected an object for an asset id, got {}.", in.type_name());
            return {};
        }

        const auto typeEntry = in.find("type");
        if (typeEntry == in.end())
        {
            LOG_ERROR("[AssetJsonSerializer] Asset id has no type.");
            return {};
        }

        AssetType type = AssetType::Unknown;
        MetaAny target = TypeRegistry::GetContext().Resolve<AssetType>().from_void(&type);
        if (!DeserializeFromJson(*typeEntry, target) || type == AssetType::Unknown)
        {
            return {};
        }

        const eastl::string path = ReadString(in, "path");
        if (path.empty())
        {
            LOG_ERROR("[AssetJsonSerializer] Asset id has no path.");
            return {};
        }

        // An absent `desc` means "the defaults", not "no descriptor": a null one hashes
        // differently from a default one, and every id built through Of<T> carries one.
        const auto descriptorEntry = in.find("desc");
        Ptr<AssetDescriptor> descriptor = DescriptorFromJson(
            type, descriptorEntry != in.end() ? *descriptorEntry : JsonValue::object());
        if (!descriptor)
        {
            return {};
        }

        const eastl::string sub = ReadString(in, "sub");
        return AssetId::Of(eastl::string_view(path.c_str(), path.size()),
                           eastl::string_view(sub.c_str(), sub.size()),
                           type, eastl::move(descriptor));
    }

    bool AssetIdToJsonField(const AssetId& id, JsonValue& out)
    {
        if (!id.IsValid())
        {
            out = nullptr;
            return true;
        }
        return AssetIdToJson(id, out);
    }

    bool AssetIdFromJsonField(const JsonValue& in, AssetId& target)
    {
        if (in.is_null())
        {
            target = {};
            return true;
        }
        target = AssetIdFromJson(in);
        return target.IsValid();
    }

    eastl::string AssetIdToDisplayString(const AssetId& id)
    {
        if (!id.IsValid())
        {
            return "None";
        }

        eastl::string text = id.GetPath();
        if (id.IsSubAsset())
        {
            text += ':';
            text += id.GetSubLabel();
        }
        return text;
    }

    bool DescriptorToJson(const AssetDescriptor& descriptor, AssetType type, JsonValue& out)
    {
        ReflectContext& context = TypeRegistry::GetContext();

        // The static_cast is safe by construction: each branch is the one that established
        // which concrete descriptor `type` names.
        switch (type)
        {
        case AssetType::Image:
        {
            const auto& typed = static_cast<const ImageAssetDescriptor&>(descriptor);
            return SerializeToJson(context.Resolve<ImageAssetDescriptor>().from_void(&typed), out);
        }
        case AssetType::Model:
        {
            const auto& typed = static_cast<const ModelAssetDescriptor&>(descriptor);
            return SerializeToJson(context.Resolve<ModelAssetDescriptor>().from_void(&typed), out);
        }
        case AssetType::Shader:
        {
            const auto& typed = static_cast<const ShaderDescriptor&>(descriptor);
            return SerializeToJson(context.Resolve<ShaderDescriptor>().from_void(&typed), out);
        }
        case AssetType::Material:
        {
            const auto& typed = static_cast<const MaterialAssetDescriptor&>(descriptor);
            return SerializeToJson(context.Resolve<MaterialAssetDescriptor>().from_void(&typed), out);
        }
        default:
            LOG_ERROR("[AssetJsonSerializer] No descriptor for asset type {}.",
                static_cast<uint32_t>(type));
            return false;
        }
    }

    Ptr<AssetDescriptor> DescriptorFromJson(AssetType type, const JsonValue& in)
    {
        ReflectContext& context = TypeRegistry::GetContext();

        // The concrete pointer is what reflection is handed; Ptr<>::get() would hand back
        // the base one.
        switch (type)
        {
        case AssetType::Image:
        {
            auto* typed = new ImageAssetDescriptor{};
            Ptr<AssetDescriptor> descriptor(typed);
            MetaAny target = context.Resolve<ImageAssetDescriptor>().from_void(typed);
            DeserializeFromJson(in, target);
            return descriptor;
        }
        case AssetType::Model:
        {
            auto* typed = new ModelAssetDescriptor{};
            Ptr<AssetDescriptor> descriptor(typed);
            MetaAny target = context.Resolve<ModelAssetDescriptor>().from_void(typed);
            DeserializeFromJson(in, target);
            return descriptor;
        }
        case AssetType::Shader:
        {
            auto* typed = new ShaderDescriptor{};
            Ptr<AssetDescriptor> descriptor(typed);
            MetaAny target = context.Resolve<ShaderDescriptor>().from_void(typed);
            DeserializeFromJson(in, target);
            return descriptor;
        }
        case AssetType::Material:
        {
            auto* typed = new MaterialAssetDescriptor{};
            Ptr<AssetDescriptor> descriptor(typed);
            MetaAny target = context.Resolve<MaterialAssetDescriptor>().from_void(typed);
            DeserializeFromJson(in, target);
            return descriptor;
        }
        default:
            LOG_ERROR("[AssetJsonSerializer] No descriptor for asset type {}.",
                static_cast<uint32_t>(type));
            return {};
        }
    }
}
