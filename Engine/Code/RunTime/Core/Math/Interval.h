#pragma once

#include <cstdint>

namespace Spark
{
    struct Interval
    {
        Interval() = default;
        Interval(uint32_t min, uint32_t max);

        uint32_t m_min = 0;
        uint32_t m_max = 0;

        bool operator == (const Interval& rhs) const;
        bool operator != (const Interval& rhs) const;

        //! Return true if it overlaps with an interval.
        //! Overlapping m_min or m_max counts as interval overlap (e.g. [0, 3] overlaps with [3, 4])
        bool Overlaps(const Interval& rhs) const;
    };
}