#include "SceneSerializer.h"

#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <ECS/WorldContext.h>
#include <Hierarchy/HierarchyComponent.h>
#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>
#include <Serialization/JsonSerializer.h>
#include <Service/Service.h>
#include <VFS/FileSystem.h>

#include <Material/Components.h>
#include <Material/MaterialContext.h>

namespace Spark::Scene
{
    namespace
    {
        using TypeTable = eastl::unordered_map<TypeId, MetaType>;

        //! Keyed by the type_info hash, which is what a storage reports. .Type("X") rewrites a
        //! meta type's id to the hash of its name, so Resolve(TypeId) would miss every one.
        TypeTable PersistentTypes()
        {
            TypeTable table;
            for (const MetaType& type : TypeRegistry::GetContext().GetAllTypes())
            {
                if (HasComponentFlag(type.traits<ComponentFlags>(), ComponentFlags::Persistent))
                {
                    table.emplace(type.info().hash(), type);
                }
            }
            return table;
        }

        template<typename E>
        uint32_t Raw(E entity)
        {
            return static_cast<uint32_t>(entity);
        }

        //! Sorted by value: a storage's own order is swap-and-pop history, and the file must
        //! not change because some unrelated entity was deleted.
        template<typename E, typename Keep>
        eastl::vector<E> SortedKept(eastl::vector<E> entities, Keep keep)
        {
            entities.erase(eastl::remove_if(entities.begin(), entities.end(),
                [&](E entity) { return !keep(entity); }), entities.end());
            eastl::sort(entities.begin(), entities.end(),
                [](E a, E b) { return Raw(a) < Raw(b); });
            return entities;
        }

        //! Membership is a fact the entity carries: in the scene graph (world), or holding an
        //! asset identity (materials). Nothing has to remember to exclude the furniture a
        //! system builds for itself, and the written set is closed under its own references --
        //! a Hierarchy link cannot point outside it.
        template<typename Membership, typename E>
        bool WriteContext(const ContextStorage<E>& context, const TypeTable& types, JsonValue& out)
        {
            using Storage = typename ContextStorage<E>::ComponentStorage;

            // Held as the erased base: a typed storage iterates its values, the base its entities.
            const Storage* members = context.template GetStorage<Membership>();
            const auto     inScene = [members](E entity)
            {
                // A tombstone is the hole an in-place-delete storage leaves behind.
                return entity != E{entt::tombstone}
                    && members != nullptr && members->contains(entity);
            };

            eastl::vector<E> live;
            if (members != nullptr)
            {
                for (E entity : *members)
                {
                    live.push_back(entity);
                }
            }

            JsonValue entities = JsonValue::array();
            for (E entity : SortedKept(eastl::move(live), inScene))
            {
                entities.push_back(Raw(entity));
            }

            struct Segment
            {
                MetaType       type;
                const Storage* storage;
            };

            eastl::vector<Segment> segments;
            for (const auto& entry : context.GetStorages())
            {
                const auto found = types.find(entry.type);
                if (found != types.end())
                {
                    segments.push_back({found->second, entry.storage});
                }
            }
            eastl::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b)
            {
                return std::strcmp(a.type.name(), b.type.name()) < 0;
            });

            bool      complete   = true;
            JsonValue components = JsonValue::object();
            for (const Segment& segment : segments)
            {
                eastl::vector<E> holders;
                for (E entity : *segment.storage)
                {
                    holders.push_back(entity);
                }

                JsonValue values = JsonValue::object();
                for (E entity : SortedKept(eastl::move(holders), inScene))
                {
                    // A component with no fields has no instance to walk: the type is the data.
                    JsonValue value = JsonValue::object();
                    if (const void* instance = segment.storage->value(entity))
                    {
                        if (!SerializeToJson(segment.type.from_void(instance), value))
                        {
                            LOG_WARN("[SceneSerializer] {} on entity {} could not be encoded.",
                                segment.type.name(), Raw(entity));
                            complete = false;
                            continue;
                        }
                    }
                    values[std::to_string(Raw(entity))] = std::move(value);
                }

                if (!values.empty())
                {
                    components[segment.type.name()] = std::move(values);
                }
            }

            out = JsonValue::object();
            out["entities"]   = std::move(entities);
            out["components"] = std::move(components);
            return complete;
        }
    }

    bool WriteScene(const ContextStorage<Entity>& world,
                    const ContextStorage<Material::MaterialHandle>& materials,
                    JsonValue& out)
    {
        const TypeTable types = PersistentTypes();

        JsonValue  contexts   = JsonValue::object();
        const bool worldOk    = WriteContext<Hierarchy>(world, types, contexts["world"]);
        const bool materialOk =
            WriteContext<Material::MaterialAssetRef>(materials, types, contexts["material"]);

        out = JsonValue::object();
        out["contexts"] = std::move(contexts);
        return worldOk && materialOk;
    }

    bool SaveScene(eastl::string_view virtualPath)
    {
        // The live scene is by definition what the two ambient contexts hold. This edge is the
        // one place that reaches for them; WriteScene is handed them explicitly.
        const auto* world      = WorldExecuteContext::Current();
        const auto* materials  = Material::MaterialExecuteContext::Current();
        const auto* fileSystem = Service<FileSystem>::Get();
        const eastl::string path(virtualPath);
        if (world == nullptr || materials == nullptr || fileSystem == nullptr)
        {
            LOG_ERROR("[SceneSerializer] {} not saved: no world, material context or file system.",
                path.c_str());
            return false;
        }

        JsonValue json;
        if (!WriteScene(*world, *materials, json))
        {
            LOG_ERROR("[SceneSerializer] {} not saved: a component could not be encoded.", path.c_str());
            return false;
        }

        const std::string text = json.dump(2);
        return fileSystem->WriteFile(virtualPath,
            reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
}
