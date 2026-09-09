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

        /// A context that dispatches nothing has nothing to repair. WorldContext overloads this.
        template<typename... Ts, typename E>
        void OnExternalWrite(BasicContext<E>&, eastl::span<const E>)
        {
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
}
