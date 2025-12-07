#include "Interval.h"

namespace Spark
{
    Interval::Interval(uint32_t min, uint32_t max)
        : m_min{min}
        , m_max{max}
    {}

    bool Interval::operator == (const Interval& rhs) const
    {
        return m_min == rhs.m_min && m_max == rhs.m_max;
    }

    bool Interval::operator != (const Interval& rhs) const
    {
        return m_min != rhs.m_min || m_max != rhs.m_max;
    }

    bool Interval::Overlaps(const Interval& rhs) const
    {
        return m_min <= rhs.m_max && rhs.m_min <= m_max;
    }
}