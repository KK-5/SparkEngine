#pragma once

#include <tuple>

#include <EASTL/span.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include "../BasicContext.h"
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

    template<typename EntityType>
    struct MergeResult
    {
        eastl::vector<EntityType> created;
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
        void MergeCreateEntity(MergeContextT<E>& target, E source, MergeResult<E>& result)
        {
            const E created = target.template GetStorage<E>().generate(source);
            if (created != source)
            {
                target.template Add<MergedTo<E>>(target.EntityAt(source), MergedTo<E>{created});
            }
            target.template Add<MergedFrom<E>>(created, MergedFrom<E>{source});
            result.created.push_back(created);
        }

        template<typename T, MergeMatch Match, typename... Ts, typename Target, typename Source, typename E>
        void MergeCreateFor(Target& target, Source& source, MergeResult<E>& result)
        {
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
                MergeCreateEntity<E>(target, entity, result);
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
                    destination.emplace(mapped, eastl::move(pool->get(entity)));
                }
                else
                {
                    destination.emplace(mapped, pool->get(entity));
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

        /// A context that dispatches nothing has nothing to repair. WorldContext overloads this.
        template<typename... Ts, typename E>
        void OnExternalWrite(BasicContext<E>&, eastl::span<const E>)
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
        void OnExternalWrite(WorldContext& target, eastl::span<const Entity> entities)
        {
            if (entities.empty())
            {
                return;
            }

            ExecuteContextGuard<Entity> guard(target);

            EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntitiesCreate, entities);

            eastl::vector<Entity> scratch;
            scratch.reserve(entities.size());
            (DispatchBatchConstruct<Ts>(target, entities, scratch), ...);
        }

        template<MergeMatch Match, MergeMapping Mapping, bool Move, typename... Ts, typename Target, typename Source>
        auto MergeInternal(Target& target, Source& source)
            -> MergeResult<typename eastl::remove_const<Source>::type::Entity>
        {
            using E = typename eastl::remove_const<Source>::type::Entity;

            static_assert(Mapping == MergeMapping::Remap,
                "MergeMapping::Identity is not implemented yet -- it lands with undo.");

            MergeResult<E> result;

            (MergeCreateFor<Ts, Match, Ts...>(target, source, result), ...);
            (MergeTransferFor<Ts, Match, Move, Ts...>(target, source), ...);

            OnExternalWrite<Ts...>(target, eastl::span<const E>(result.created.data(), result.created.size()));

            target.template Clear<MergedFrom<E>, MergedTo<E>>();
            return result;
        }
    }

    /// @brief Move the components of the listed types out of a staging context and into a live one.
    ///
    /// The source is moved out of and dies with the call.
    template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
    MergeResult<E> Merge(MergeContextT<E>& target, StagingContext<E>&& source)
    {
        static_assert(sizeof...(Ts) > 0, "Merge needs at least one component type.");
        return Internal::MergeInternal<Match, Mapping, true, Ts...>(target, source);
    }

    /// @brief Copy the components of the listed types from a staging context into a live one.
    ///
    /// The source stays intact and can be merged again -- this is how a prefab is instantiated more
    /// than once. Component types that cannot be copied fail to compile here rather than dropping
    /// data at runtime.
    template<MergeMatch Match, MergeMapping Mapping, typename... Ts, typename E>
    MergeResult<E> Merge(MergeContextT<E>& target, const StagingContext<E>& source)
    {
        static_assert(sizeof...(Ts) > 0, "Merge needs at least one component type.");
        return Internal::MergeInternal<Match, Mapping, false, Ts...>(target, source);
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
