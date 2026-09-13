#include "SceneSerializer.h"

#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>
#include <ECS/WorldContext.h>
#include <ECS/Merge/ContextMerge.h>
#include <ECS/StagingContext.h>
#include <Hierarchy/HierarchyComponent.h>
#include <Log/ILogSystem.h>
#include <Reflection/TypeRegistry.h>
#include <HashString/HashString.h>
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

        //! A component that goes into the file has to be able to come back out of one. The two
        //! are declared apart -- Persistent on the traits, the runtime binding on the meta type --
        //! so say which type would be read back as nothing.
        void CheckPersistentTypesCanBeRead()
        {
            for (const MetaType& type : TypeRegistry::GetContext().GetAllTypes())
            {
                if (HasComponentFlag(type.traits<ComponentFlags>(), ComponentFlags::Persistent)
                    && !type.func("ComponentStorage"_hs))
                {
                    LOG_ERROR("[SceneSerializer] {} is Persistent but takes no runtime data; "
                              "it is written and never read back.", type.info().name());
                }
            }
        }

        //! A key in the file is an identifier written as itself.
        template<typename E>
        bool ReadHandle(const std::string& key, E& out)
        {
            char*             end   = nullptr;
            const unsigned long raw = std::strtoul(key.c_str(), &end, 10);
            if (end == key.c_str() || *end != '\0')
            {
                return false;
            }
            out = static_cast<E>(static_cast<uint32_t>(raw));
            return true;
        }

        //! One context's half of the file into a staging context. Nothing live is touched here:
        //! a file that fails half way has to leave both contexts as they were.
        template<typename E>
        bool ReadContext(const JsonValue& in, StagingContext<E>& staging)
        {
            const auto entities = in.find("entities");
            if (entities == in.end() || !entities->is_array())
            {
                LOG_ERROR("[SceneSerializer] A context needs an entities array.");
                return false;
            }

            // Every identifier first: a component can only be attached to an entity that exists,
            // and the list is what says which identifiers this scene owns.
            for (const JsonValue& entry : *entities)
            {
                if (!entry.is_number_unsigned())
                {
                    LOG_ERROR("[SceneSerializer] An entity identifier is an unsigned number.");
                    return false;
                }
                staging.CreateEntity(static_cast<E>(entry.get<uint32_t>()));
            }

            const auto components = in.find("components");
            if (components == in.end())
            {
                return true;
            }

            for (const auto& segment : components->items())
            {
                // The segment name is the reflected type name, which .Type() made the meta id.
                const MetaType type = TypeRegistry::GetContext().Resolve(
                    HashString::value(segment.key().c_str()));
                if (!type)
                {
                    LOG_WARN("[SceneSerializer] No type named {}; its components are skipped.",
                        segment.key());
                    continue;
                }

                for (const auto& item : segment.value().items())
                {
                    E entity{};
                    if (!ReadHandle(item.key(), entity))
                    {
                        LOG_ERROR("[SceneSerializer] {} is not an entity identifier.", item.key());
                        return false;
                    }

                    MetaAny instance = type.construct();
                    if (!instance || !DeserializeFromJson(item.value(), instance))
                    {
                        LOG_ERROR("[SceneSerializer] {} on entity {} could not be decoded.",
                            segment.key(), item.key());
                        return false;
                    }

                    if (!staging.Add(entity, instance))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        //! Material handles in the world half name identifiers from the file, and the material
        //! merge may have moved them. Their own merge will not do it: it rewrites references of
        //! its own entity type, and a MaterialHandle is not one.
        void TranslateMaterialRefs(const Material::MaterialContext& materials,
                                   StagingContext<Entity>& world)
        {
            for (const auto& entry : world.GetStorages())
            {
                const MetaType type = TypeRegistry::GetContext().Resolve(entry.storage->type());
                auto* storage = type ? RuntimeComponentStorage<Entity>(type, world) : nullptr;
                if (storage == nullptr)
                {
                    continue;
                }

                for (Entity entity : *storage)
                {
                    if (void* component = storage->value(entity); component != nullptr)
                    {
                        TranslateMergedRefs<Material::MaterialHandle>(materials, type, component);
                    }
                }
            }
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

    bool ReadScene(const JsonValue& in, WorldContext& world, Material::MaterialContext& materials)
    {
        const auto contexts = in.find("contexts");
        if (contexts == in.end() || !contexts->is_object())
        {
            LOG_ERROR("[SceneSerializer] A scene needs a contexts object.");
            return false;
        }

        // Both halves are decoded before either is merged, so a file that turns out to be bad
        // leaves the live contexts untouched -- the reading half of "nothing is written unless
        // every component encoded".
        CheckPersistentTypesCanBeRead();

        StagingContext<Material::MaterialHandle> materialStaging;
        StagingContext<Entity>                   worldStaging;

        const auto material = contexts->find("material");
        if (material != contexts->end() && !ReadContext(*material, materialStaging))
        {
            return false;
        }

        const auto section = contexts->find("world");
        if (section != contexts->end() && !ReadContext(*section, worldStaging))
        {
            return false;
        }

        // Materials first, and their records are kept: the world half names them by the
        // identifiers the file used, which this merge may have moved.
        Merge(materials, eastl::move(materialStaging), MergeRecords::Keep);
        TranslateMaterialRefs(materials, worldStaging);
        Merge(world, eastl::move(worldStaging));
        ClearMergeRecords(materials);
        return true;
    }

    bool LoadScene(eastl::string_view virtualPath)
    {
        auto* world      = WorldExecuteContext::Current();
        auto* materials  = Material::MaterialExecuteContext::Current();
        auto* fileSystem = Service<FileSystem>::Get();
        const eastl::string path(virtualPath);
        if (world == nullptr || materials == nullptr || fileSystem == nullptr)
        {
            LOG_ERROR("[SceneSerializer] {} not loaded: no world, material context or file system.",
                path.c_str());
            return false;
        }

        eastl::vector<uint8_t> bytes;
        if (!fileSystem->ReadFile(virtualPath, bytes))
        {
            LOG_ERROR("[SceneSerializer] {} could not be read.", path.c_str());
            return false;
        }

        const JsonValue json = JsonValue::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (json.is_discarded())
        {
            LOG_ERROR("[SceneSerializer] {} is not valid JSON.", path.c_str());
            return false;
        }

        return ReadScene(json, *world, *materials);
    }

    void ClearScene(WorldContext& world, Material::MaterialContext& materials)
    {
        for (Entity entity : world.GetView<Hierarchy>(Exclude<DeadTag>))
        {
            world.Add<DeadTag>(entity);
        }

        // The material context has no reaper, and nothing observes a material entity's death.
        eastl::vector<Material::MaterialHandle> dead;
        for (Material::MaterialHandle handle : materials.GetView<Material::MaterialAssetRef>())
        {
            dead.push_back(handle);
        }
        materials.DestoryEntity(dead.begin(), dead.end());
    }
}
