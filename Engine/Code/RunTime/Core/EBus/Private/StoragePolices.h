#pragma once

#include <EASTL/intrusive_list.h>
#include <EASTL/unordered_map.h>
#include <EASTL/map.h>

#include "Polices.h"

namespace Spark
{
    // AddressStoragePolicy
    template <typename Traits, typename HandlerHolder, EBusAddressPolicy = Traits::AddressPolicy>
    struct AddressStoragePolicy;

    template <typename Traits, typename HandlerHolder>
    struct AddressStoragePolicy<Traits, HandlerHolder, EBusAddressPolicy::ById>
    {
    private:
        using IdType = typename Traits::BusIdType;
        
    public:
        struct StorageType: public eastl::unordered_map<IdType, HandlerHolder>
        {
            using Base = eastl::unordered_map<IdType, HandlerHolder>;
            
            StorageType(): Base(typename Traits::AllocatorType()) {}

            template <typename... InputArgs>
            typename Base::iterator emplace(InputArgs&&... args)
            {
                auto [iter, inserted] = Base::emplace(eastl::forward<InputArgs>(args)...);
                assert(inserted && "[EBus] Failed to insert");
                return iter;
            }
            
            void erase(const IdType& id)
            {
                Base::erase(id);
            }
        };
    };

    template <typename Traits, typename HandlerHolder>
    struct AddressStoragePolicy<Traits, HandlerHolder, EBusAddressPolicy::ByIdAndOrdered>
    {
    private:
        using IdType = typename Traits::BusIdType;
    
    public:
        struct StorageType: public eastl::map<IdType, HandlerHolder>
        {
            using Base = eastl::map<IdType, HandlerHolder>;

            StorageType(): Base(typename Traits::AllocatorType()) {}

            template <typename... InputArgs>
            typename Base::iterator emplace(InputArgs&&... args)
            {
                auto [iter, inserted] = Base::emplace(eastl::forward<InputArgs>(args)...);
                assert(inserted && "[EBus] Failed to insert");
                return iter;
            }

            void erase(const IdType& id)
            {
                Base::erase(id);
            }
        };
    };

    // HandlerStoragePolicy
    template <typename Interface, typename Traits, typename Comparer = typename Traits::BusHandlerOrderCompare>
    struct HandlerCompare
        : public Comparer
    { };
    template <typename Interface, typename Traits>
    struct HandlerCompare<Interface, Traits, BusHandlerCompareDefault>
    {
        bool operator()(const Interface* left, const Interface* right) const { return left->Compare(right); }
    };

    template <typename Interface, typename Traits, typename Handler, EBusHandlerPolicy = Traits::HandlerPolicy>
    struct HandlerStoragePolicy;

    template <typename Interface, typename Traits, typename HandlerNode>
    struct HandlerStoragePolicy<Interface, Traits, HandlerNode, EBusHandlerPolicy::Multiple>
    {
    public:
        struct StorageType
            : public eastl::intrusive_list<HandlerNode>
        {
            using Base = eastl::intrusive_list<HandlerNode>;

            void insert(HandlerNode& elem)
            {
                Base::push_front(elem);
            }

            void erase(HandlerNode& elem)
            {
                Base::remove(elem);
            }
        };
    };

    template <typename Interface, typename Traits, typename HandlerNode>
    struct HandlerStoragePolicy<Interface, Traits, HandlerNode, EBusHandlerPolicy::MultipleAndOrdered>
    {
    private:
        using Compare = HandlerCompare<Interface, Traits>;
    public:
        // 这里应该使用intrusive_multiset来实现，但是eastl没有这个容器，这里用intrusive_list来模拟
        struct StorageType
            : public eastl::intrusive_list<HandlerNode>
        {
            using Base = eastl::intrusive_list<HandlerNode>;

            void insert(HandlerNode& elem)
            {
                auto it = Base::begin();
                while(it != Base::end())
                {
                    if (Compare{}(elem, *it))
                    {
                        Base::insert(it, elem);
                        return;
                    }
                    it++;
                }

                Base::push_back(elem);
            }

            void erase(HandlerNode& elem)
            {
                Base::remove(elem);
            }
        };
    };

    // HandlerStorageNode
    template <typename Handler, EBusHandlerPolicy>
    struct HandlerStorageNode
    {
    };
    template <typename Handler>
    struct HandlerStorageNode<Handler, EBusHandlerPolicy::Multiple>
        : public eastl::intrusive_list_node
    {
    };
    template <typename Handler>
    struct HandlerStorageNode<Handler, EBusHandlerPolicy::MultipleAndOrdered>
        : public eastl::intrusive_list_node
    {
    };
}