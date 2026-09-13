#pragma once

#include <tuple>

#include <EASTL/span.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include <Log/ILogSystem.h>

#include <Reflection/TypeRegistry.h>

#include "../BasicContext.h"
#include "../ComponentTraits.h"
#include "../ExecuteContext.h"
#include "../StagingContext.h"
#include "../WorldContext.h"
#include "MergeComponents.h"

namespace Spark
{
    /// Which source entities take part: those carrying any of Ts, or those carrying all of them.
    enum class MergeMatch
    {
        Any,
        All
    };

    /// How a source entity maps to a target one.
    enum class MergeMapping
    {
        Remap,      //!< A target entity is created for it; the mapping is identity except on collision.
        Identity    //!< The source identifier is already the target one. Not implemented yet.
    };

    template<typename EntityType>
    using MergeContextT = typename ContextTraits<EntityType>::ContextType;

    /// What to do with the records a merge leaves in the target: MergedFrom on every entity it
    /// created, MergedTo wherever an identifier had to move.
    ///
    /// They ARE the answer to "what landed" and "where did this source go", so a caller that
    /// still has questions -- a second context to translate against, say -- keeps them and clears
    /// them itself. Merging again while they stand would read two batches as one.
    enum class MergeRecords
    {
        Clear,
        Keep
    };

    namespace Internal
    {
        /// Source -> target. The mapping is identity unless the identifier was taken, in which case
        /// the entity that took it carries the exception.
        ///
        /// Only meaningful for source entities that took part in this merge: an outsider falls
        /// through to identity rather than reporting a miss.
        template<typename E>
        E MergeForward(const MergeContextT<E>& target, E source)
        {
            const E occupant = target.EntityAt(source);
            if (occupant != E{entt::null} && target.template Has<MergedTo<E>>(occupant))
            {
                return target.template Get<MergedTo<E>>(occupant).entity;
            }
            return source;
        }

        /// Under MergeMatch::Any an entity is visited once per component type it carries, so the
        /// second visit has to recognise the first. MergedFrom is that record.
        template<typename E>
        bool MergeAlreadyCreated(const MergeContextT<E>& target, E source)
        {
            const E created = MergeForward<E>(target, source);
            return target.Valid(created)
                && target.template Has<MergedFrom<E>>(created)
                && target.template Get<MergedFrom<E>>(created).source == source;
        }

        /// Rewrites the entity references a component declares, in place, after it has landed in
        /// the target.
        ///
        /// A reference of type E names a source entity. If that entity did not take part, nothing
        /// in the target answers to it: keeping the identifier would silently name an unrelated
        /// live entity, so the reference is dropped instead.
        template<typename E, typename T>
        void MergeTranslate(const MergeContextT<E>& target, T& component)
        {
            for (E* ref : GetEntityRefs<E>(component))
            {
                if (*ref == E{entt::null})
                {
                    continue;
                }

                if (MergeAlreadyCreated<E>(target, *ref))
                {
                    *ref = MergeForward<E>(target, *ref);
                }
                else
                {
                    LOG_ERROR("[Merge] Dropped a reference to a source entity that did not take part.");
                    *ref = E{entt::null};
                }
            }
        }

        /// Never creates the storage: a type absent from the source is simply not merged.
        template<typename T, typename Source>
        auto MergeSourceStorage(Source& source)
        {
            const auto* found = eastl::as_const(source).template GetStorage<T>();
            if constexpr (eastl::is_const<Source>::value)
            {
                return found;
            }
            else
            {
                return found != nullptr ? &source.template GetStorage<T>() : nullptr;
            }
        }

        template<MergeMatch Match, typename... Ts, typename Source, typename E>
        bool MergeMatches([[maybe_unused]] const Source& source, [[maybe_unused]] E entity)
        {
            if constexpr (Match == MergeMatch::All)
            {
                return source.template HasAll<Ts...>(entity);
            }
            else
            {
                return true;
            }
        }

        /// Creates the target entity through the entity storage rather than CreateEntity, so no
        /// per-entity event is dispatched -- the batch is announced once, after everything moved.
        template<typename E>
        void MergeCreateEntity(MergeContextT<E>& target, E source)
        {
            const E created = target.template GetStorage<E>().generate(source);
            if (created != source)
            {
                target.template Add<MergedTo<E>>(target.EntityAt(source), MergedTo<E>{created});
            }
            target.template Add<MergedFrom<E>>(created, MergedFrom<E>{source});
        }

        template<typename T, MergeMatch Match, typename... Ts, typename Target, typename Source>
        void MergeCreateFor(Target& target, Source& source)
        {
            using E = typename eastl::remove_const<Source>::type::Entity;

            auto* pool = MergeSourceStorage<T>(source);
            if (pool == nullptr)
            {
                return;
            }

            // A storage iterates its elements; its sparse-set base iterates the identifiers.
            const typename entt::basic_registry<E>::common_type& identifiers = *pool;
            for (E entity : identifiers)
            {
                if (!MergeMatches<Match, Ts...>(source, entity))
                {
                    continue;
                }
                if (MergeAlreadyCreated<E>(target, entity))
                {
                    continue;
                }
                MergeCreateEntity<E>(target, entity);
            }
        }

        template<typename T, MergeMatch Match, bool Move, typename... Ts, typename Target, typename Source>
        void MergeTransferFor(Target& target, Source& source)
        {
            using E = typename eastl::remove_const<Source>::type::Entity;

            auto* pool = MergeSourceStorage<T>(source);
            if (pool == nullptr)
            {
                return;
            }

            auto& destination = target.template GetStorage<T>();
            const typename entt::basic_registry<E>::common_type& identifiers = *pool;
            for (E entity : identifiers)
            {
                if (!MergeMatches<Match, Ts...>(source, entity))
                {
                    continue;
                }

                const E mapped = MergeForward<E>(target, entity);
                if constexpr (std::tuple_size_v<decltype(pool->get_as_tuple(E{}))> == 0u)
                {
                    destination.emplace(mapped);
                }
                else if constexpr (Move)
                {
                    MergeTranslate<E>(target, destination.emplace(mapped, eastl::move(pool->get(entity))));
                }
                else
                {
                    MergeTranslate<E>(target, destination.emplace(mapped, pool->get(entity)));
                }
            }
        }

        /// Extract's own transfer: identifiers are restored verbatim into an empty staging context,
        /// so there is no mapping to consult.
        template<typename T, MergeMatch Match, typename... Ts, typename E, typename Source>
        void ExtractFor(StagingContext<E>& staging, const Source& source)
        {
            const auto* pool = MergeSourceStorage<T>(source);
            if (pool == nullptr)
            {
                return;
            }

            auto& destination = staging.template GetStorage<T>();
            auto& identifiers = staging.template GetStorage<E>();
            const typename entt::basic_registry<E>::common_type& sourceIdentifiers = *pool;

            for (E entity : sourceIdentifiers)
            {
                if (!MergeMatches<Match, Ts...>(source, entity))
                {
                    continue;
                }

                if (!staging.Valid(entity))
                {
                    identifiers.generate(entity);
                }

                if constexpr (std::tuple_size_v<decltype(pool->get_as_tuple(E{}))> == 0u)
                {
                    destination.emplace(entity);
                }
                else
                {
                    destination.emplace(entity, pool->get(entity));
                }
            }
        }

        /// The entities a merge just created -- the record it left on each of them. Answerable
        /// until the records are cleared.
        template<typename E, typename Target>
        eastl::vector<E> MergedBatch(Target& target)
        {
            eastl::vector<E> entities;
            for (E entity : target.template GetView<MergedFrom<E>>())
            {
                entities.push_back(entity);
            }
            return entities;
        }

        /// Records a previous merge kept and never cleared would read as part of this batch.
        template<typename E, typename Target>
        void DropStaleRecords(Target& target)
        {
            if (!MergedBatch<E>(target).empty())
            {
                LOG_ERROR("[Merge] The previous merge kept its records and never cleared them.");
                target.template Clear<MergedFrom<E>, MergedTo<E>>();
            }
        }

        /// A context that dispatches nothing has nothing to repair. WorldContext overloads this.
        template<typename... Ts, typename E>
        void OnExternalWrite(BasicContext<E>&)
        {
        }

        /// One event per component type, carrying only the entities that actually carry it.
        template<typename T>
        void DispatchBatchConstruct(WorldContext& target, eastl::span<const Entity> entities,
            eastl::vector<Entity>& scratch)
        {
            if constexpr ((ComponentTraits<T>::componentEvents & ComponentEventMask::Create) != ComponentEventMask::None)
            {
                const auto& pool = target.template GetStorage<T>();

                scratch.clear();
                for (Entity entity : entities)
                {
                    if (pool.contains(entity))
                    {
                        scratch.push_back(entity);
                    }
                }

                if (!scratch.empty())
                {
                    ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentsConstruct,
                        eastl::span<const Entity>(scratch.data(), scratch.size()));
                }
            }
        }

        /// Handlers reach their context through WorldExecuteContext::Current(), so the target has to
        /// be the current one while they run.
        template<typename... Ts>
        void OnExternalWrite(WorldContext& target)
        {
            const eastl::vector<Entity> entities = MergedBatch<Entity>(target);
            if (entities.empty())
            {
                return;
            }

            ExecuteContextGuard<Entity> guard(target);

            EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntitiesCreate,
                eastl::span<const Entity>(entities.data(), entities.size()));

            eastl::vector<Entity> scratch;
            scratch.reserve(entities.size());
            (DispatchBatchConstruct<Ts>(target,
                eastl::span<const Entity>(entities.data(), entities.size()), scratch), ...);
        }

        template<MergeMatch Match, MergeMapping Mapping, bool Move, typename... Ts, typename Target, typename Source>
        void MergeInternal(Target& target, Source& source, MergeRecords records)
        {
            using E = typename eastl::remove_const<Source>::type::Entity;

            static_assert(Mapping == MergeMapping::Remap,
                "MergeMapping::Identity is not implemented yet -- it lands with undo.");

            DropStaleRecords<E>(target);

            (MergeCreateFor<Ts, Match, Ts...>(target, source), ...);
            (MergeTransferFor<Ts, Match, Move, Ts...>(target, source), ...);

            OnExternalWrite<Ts...>(target);

            if (records == MergeRecords::Clear)
            {
                target.template Clear<MergedFrom<E>, MergedTo<E>>();
            }
        }

        //! One segment of a runtime merge: a component type and the target storage it landed in.
        //! Which of the batch carries it is asked of the storage, exactly as the typed form does.
        template<typename E>
        struct MergeSegment
        {
            TypeId                                                 type;
            const typename entt::basic_registry<E>::common_type*   storage;
        };

        template<typename E>
        void OnExternalWriteRuntime(BasicContext<E>&, const eastl::vector<MergeSegment<E>>&)
        {
        }

        inline void OnExternalWriteRuntime(WorldContext& target,
            const eastl::vector<MergeSegment<Entity>>& segments)
        {
            const eastl::vector<Entity> entities = MergedBatch<Entity>(target);
            if (entities.empty())
            {
                return;
            }

            ExecuteContextGuard<Entity> guard(target);

            EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntitiesCreate,
                eastl::span<const Entity>(entities.data(), entities.size()));

            // componentEvents is not consulted: the bus is keyed by TypeId, so announcing a
            // type nobody listens to costs one empty broadcast -- cheaper than mirroring the
            // mask into reflection for the sake of skipping it.
            eastl::vector<Entity> scratch;
            scratch.reserve(entities.size());
            for (const MergeSegment<Entity>& segment : segments)
            {
                scratch.clear();
                for (Entity entity : entities)
                {
                    if (segment.storage->contains(entity))
                    {
                        scratch.push_back(entity);
                    }
                }

                if (!scratch.empty())
                {
                    ComponentEventBus::Event(segment.type, &ComponentEventBus::Events::OnComponentsConstruct,
                        eastl::span<const Entity>(scratch.data(), scratch.size()));
                }
            }
        }

        //! Does any reflected field of this type name an entity of E? Asked once per segment,
        //! so a type without references pays nothing per component.
        template<typename E>
        bool HasReflectedEntityRefs(const MetaType& type)
        {
            for (auto&& [id, data] : type.data())
            {
                if (data.type().info() == GetTypeInfo<E>())
                {
                    return true;
                }
            }
            return false;
        }

        //! The runtime counterpart of MergeTranslate. A field whose TYPE is E is a reference --
        //! the rule the encoder already uses -- so nothing has to be marked a second time.
        template<typename E>
        void MergeTranslateReflected(const MergeContextT<E>& target, const MetaType& type, void* component)
        {
            MetaAny handle = type.from_void(component);
            if (!handle)
            {
                return;
            }

            for (auto&& [id, data] : type.data())
            {
                if (data.type().info() != GetTypeInfo<E>())
                {
                    continue;
                }

                MetaAny field = data.get(handle);
                const E source = field ? field.cast<E>() : E{entt::null};
                if (source == E{entt::null})
                {
                    continue;
                }

                if (MergeAlreadyCreated<E>(target, source))
                {
                    data.set(handle, MergeForward<E>(target, source));
                }
                else
                {
                    LOG_ERROR("[Merge] Dropped a reference to a source entity that did not take part.");
                    data.set(handle, E{entt::null});
                }
            }
        }

        template<typename E>
        void MergeRuntime(MergeContextT<E>& target, StagingContext<E>& source, MergeRecords records)
        {
            DropStaleRecords<E>(target);

            const auto storages = source.GetStorages();

            // Every identifier the source carries anything for takes part. All of them are
            // created before anything moves, which is what makes a reference inside the batch
            // resolvable while its component lands.
            for (const auto& entry : storages)
            {
                for (E entity : *entry.storage)
                {
                    if (!MergeAlreadyCreated<E>(target, entity))
                    {
                        MergeCreateEntity<E>(target, entity);
                    }
                }
            }

            eastl::vector<MergeSegment<E>> segments;
            for (const auto& entry : storages)
            {
                const MetaType type = TypeRegistry::GetContext().Resolve(entry.storage->type());
                auto* destination = RuntimeComponentStorage<E>(type, target);
                if (destination == nullptr)
                {
                    LOG_ERROR("[Merge] {} takes no runtime data; its components are dropped.",
                        entry.storage->type().name());
                    continue;
                }

                const bool translate = HasReflectedEntityRefs<E>(type);

                for (E entity : *entry.storage)
                {
                    const E mapped = MergeForward<E>(target, entity);
                    destination->push(mapped, entry.storage->value(entity));
                    if (translate)
                    {
                        if (void* landed = destination->value(mapped); landed != nullptr)
                        {
                            MergeTranslateReflected<E>(target, type, landed);
                        }
                    }
                }
                segments.push_back({entry.type, destination});
            }

            OnExternalWriteRuntime(target, segments);

            if (records == MergeRecords::Clear)
            {
                target.template Clear<MergedFrom<E>, MergedTo<E>>();
            }
        }
    }

    /// @brief Move the components of the listed types out of a staging context and into a live one.
    ///
    /// The source is moved out of and dies with the call.
    template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
    void Merge(MergeContextT<E>& target, StagingContext<E>&& source,
        MergeRecords records = MergeRecords::Clear)
    {
        static_assert(sizeof...(Ts) > 0, "Merge needs at least one component type.");
        Internal::MergeInternal<Match, Mapping, true, Ts...>(target, source, records);
    }

    /// @brief Copy the components of the listed types from a staging context into a live one.
    ///
    /// The source stays intact and can be merged again -- this is how a prefab is instantiated more
    /// than once. Component types that cannot be copied fail to compile here rather than dropping
    /// data at runtime.
    template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
    void Merge(MergeContextT<E>& target, const StagingContext<E>& source,
        MergeRecords records = MergeRecords::Clear)
    {
        static_assert(sizeof...(Ts) > 0, "Merge needs at least one component type.");
        Internal::MergeInternal<Match, Mapping, false, Ts...>(target, source, records);
    }

    /// @brief Move a staging context into a live one with no compile-time type list: what takes
    /// part is whatever the source holds.
    ///
    /// The form for data that arrived as bytes -- a scene file, a script, the network. Three
    /// differences from the typed form, all forced by erasure:
    ///   - every source type must be reflected and registered with ComponentRuntime<T>;
    ///   - components are copied, not moved: the erased storage takes an opaque element;
    ///   - the match is Any, since a condition across types cannot be spelled at runtime.
    template<typename E>
    void Merge(MergeContextT<E>& target, StagingContext<E>&& source,
        MergeRecords records = MergeRecords::Clear)
    {
        Internal::MergeRuntime<E>(target, source, records);
    }

    /// @brief Drop the records a Keep merge left behind. The next merge into this context needs
    /// them gone, or it would read the two batches as one.
    template<typename Context>
    void ClearMergeRecords(Context& target)
    {
        using E = typename Context::Entity;
        target.template Clear<MergedFrom<E>, MergedTo<E>>();
    }

    /// @brief Rewrite one component's references of type E against another context's merge
    /// records, dropping those that took no part.
    ///
    /// The cross-context case: a world component naming a material the material merge moved.
    /// Within one context a merge does this itself.
    template<typename E>
    void TranslateMergedRefs(const MergeContextT<E>& mapping, const MetaType& type, void* component)
    {
        if (Internal::HasReflectedEntityRefs<E>(type))
        {
            Internal::MergeTranslateReflected<E>(mapping, type, component);
        }
    }

    /// @brief Where a source identifier landed, or null when it took no part in the batch.
    /// Only answerable while the records stand -- which is what MergeRecords::Keep is for.
    template<typename E>
    E MergedEntity(const MergeContextT<E>& target, E source)
    {
        return Internal::MergeAlreadyCreated<E>(target, source)
            ? Internal::MergeForward<E>(target, source)
            : E{entt::null};
    }

    /// @brief Copy the components of the listed types out of a live context into a fresh staging one.
    ///
    /// The reverse direction of a Merge, and a separate operation because the source is the live
    /// side here. Both of merge's hard parts fall away: the staging context is empty so every
    /// identifier is restored verbatim, and nothing observes it so there is nobody to notify.
    template<MergeMatch Match, typename... Ts, typename Source>
    auto Extract(const Source& source) -> StagingContext<typename Source::Entity>
    {
        using E = typename Source::Entity;

        static_assert(sizeof...(Ts) > 0, "Extract needs at least one component type.");
        static_assert(eastl::is_same<Source, MergeContextT<E>>::value,
            "Extract reads a live context. Its result is the staging one.");

        StagingContext<E> staging;
        (Internal::ExtractFor<Ts, Match, Ts...>(staging, source), ...);
        return staging;
    }
}
