#pragma once

#include <EASTL/vector.h>

namespace Spark
{
    template<class T, class Aggregator>
    struct EBusReduceResult
    {
        /**
         * The current value, which new values will be aggregated with.
         */
        T value;

        /**
         * The function object that aggregates a new value with an existing value.
         */
        Aggregator unary;

        /**
         * Creates an instance of the class without setting an initial value or
         * a function object to use as the aggregator.
         */
        EBusReduceResult() {}

        /**
         * Creates an instance of the class and sets the initial value and the function
         * object to use as the aggregator.
         * @param initialValue The initial value, which new values will be aggregated with.
         * @param aggregator A function object to aggregate the values.
         * For examples of function objects that you can use as aggregators,
         * see functional_basic.h.
         */
        EBusReduceResult(const T& initialValue, const Aggregator& aggregator = Aggregator())
            : value(initialValue)
            , unary(aggregator)
        { }
        
        /**
         * Overloads the assignment operator to aggregate a new value with
         * the existing aggregated value.  Used ONLY when the return value of the function is const, or const&
         */
        void operator=(const T& rhs) { value = unary(value, rhs); }

        /**
         * Overloads the assignment operator to aggregate a new value with
         * the existing aggregated value using rvalue-ref to move
         */
        void operator=(T&& rhs) { value = unary(value, eastl::move(rhs)); }

        /**
         * Disallows copying an EBusReduceResult object by reference.
         */
        EBusReduceResult& operator=(const EBusReduceResult&) = delete;
    };

    template<class T, class Aggregator>
    struct EBusReduceResult<T&, Aggregator>
    {
        /**
         * A reference to the current value, which new values will be aggregated with.
         */
        T& value;

        /**
         * The function object that aggregates a new value with an existing value.
         */
        Aggregator unary;

        /**
         * Creates an instance of the class, sets the reference to the initial value,
         * and sets the function object to use as the aggregator.
         * @param rhs A reference to the initial value, which new values will
         * be aggregated with.
         * @param aggregator A function object to aggregate the values.
         * For examples of function objects that you can use as aggregators,
         * see functional_basic.h.
         */
        explicit EBusReduceResult(T& rhs, const Aggregator& aggregator = Aggregator())
            : value(rhs)
            , unary(aggregator)
        { }

        /**
         * Overloads the assignment operator to aggregate a new value with
         * the existing aggregated value using rvalue-ref
         */
        void operator=(T&& rhs)          { value = unary(value, eastl::move(rhs)); }

        /**
        * Overloads the assignment operator to aggregate a new value with
        * the existing aggregated value.  Used only when the return type is const, or const&
        */
        void operator=(const T& rhs) { value = unary(value, rhs); }


        /**
         * Disallows copying an EBusReduceResult object by reference.
         */
        EBusReduceResult& operator=(const EBusReduceResult&) = delete;
    };

    template<class T>
    struct EBusAggregateResults
    {
        /**
         * A vector that contains handler results.
         */
        eastl::vector<T> values;

        /**
         * Overloads the assignment operator to add a new result to a vector 
         * of previous results.
         * This const T& version is required to support const& returning functions.
         */
        void operator=(const T& rhs) { values.push_back(rhs); }
        
        /**
         * Overloads the assignment operator to add a new result to a vector 
         * of previous results, using rvalue-reference to move
         */
        void operator=(T&& rhs) { values.push_back(eastl::move(rhs)); }
    };
}