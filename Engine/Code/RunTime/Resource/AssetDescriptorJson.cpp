#include "AssetDescriptorJson.h"

#include <nlohmann/json.hpp>

#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>
#include <Serialization/JsonSerializer.h>

#include "Image/ImageAsset.h"
#include "Model/ModelAsset.h"
#include "Shader/ShaderAsset.h"

namespace Spark::Resource
{
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
        default:
            LOG_ERROR("[AssetDescriptorJson] No descriptor for asset type {}.",
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
        default:
            LOG_ERROR("[AssetDescriptorJson] No descriptor for asset type {}.",
                static_cast<uint32_t>(type));
            return {};
        }
    }
}
