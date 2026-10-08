#include "ImageSubresourceStates.h"

#include <EASTL/algorithm.h>

#include <Math/Bit.h>
#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    namespace
    {
        ImageAspectFlags AspectFlag(uint32_t aspectIndex)
        {
            return static_cast<ImageAspectFlags>(BIT(aspectIndex));
        }

        bool CoversSameSlices(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs)
        {
            return lhs.m_mipSliceMin == rhs.m_mipSliceMin
                && lhs.m_mipSliceMax == rhs.m_mipSliceMax
                && lhs.m_arraySliceMin == rhs.m_arraySliceMin
                && lhs.m_arraySliceMax == rhs.m_arraySliceMax;
        }
    }

    ImageSubresourceStates::ImageSubresourceStates(
        uint16_t mipLevels, uint16_t arraySize, ImageAspectFlags aspects, ResourceState state)
        : m_mipLevels{ mipLevels }
        , m_arraySize{ arraySize }
        , m_aspects{ aspects }
        , m_uniform{ state }
    {
        ASSERT(mipLevels > 0 && arraySize > 0,
            "[ImageSubresourceStates] An image has at least one mip and one array slice.");
        ASSERT(aspects != ImageAspectFlags::None && aspects != ImageAspectFlags::All,
            "[ImageSubresourceStates] Aspects must be the ones the image's format has.");
    }

    bool ImageSubresourceStates::IsUniform() const
    {
        return m_states.empty();
    }

    ResourceState ImageSubresourceStates::GetUniform() const
    {
        ASSERT(IsUniform(), "[ImageSubresourceStates] The image's subresources are in different states.");
        return m_uniform;
    }

    void ImageSubresourceStates::SetUniform(ResourceState state)
    {
        m_uniform = state;
        m_states.clear();
    }

    ImageSubresourceRange ImageSubresourceStates::Normalize(const ImageSubresourceRange& range) const
    {
        ASSERT(range.m_mipSliceMin < m_mipLevels && range.m_mipSliceMin <= range.m_mipSliceMax,
            "[ImageSubresourceStates] Mip range [{}, {}] is outside the image's {} mips.",
            range.m_mipSliceMin, range.m_mipSliceMax, m_mipLevels);
        ASSERT(range.m_arraySliceMin < m_arraySize && range.m_arraySliceMin <= range.m_arraySliceMax,
            "[ImageSubresourceStates] Array range [{}, {}] is outside the image's {} slices.",
            range.m_arraySliceMin, range.m_arraySliceMax, m_arraySize);

        ImageSubresourceRange normalized;
        normalized.m_mipSliceMin   = range.m_mipSliceMin;
        normalized.m_mipSliceMax   = eastl::min(range.m_mipSliceMax, static_cast<uint16_t>(m_mipLevels - 1));
        normalized.m_arraySliceMin = range.m_arraySliceMin;
        normalized.m_arraySliceMax = eastl::min(range.m_arraySliceMax, static_cast<uint16_t>(m_arraySize - 1));
        normalized.m_aspectFlags   = range.m_aspectFlags & m_aspects;
        ASSERT(normalized.m_aspectFlags != ImageAspectFlags::None,
            "[ImageSubresourceStates] The range names no aspect the image has.");
        return normalized;
    }

    bool ImageSubresourceStates::IsWholeImage(const ImageSubresourceRange& range) const
    {
        const ImageSubresourceRange normalized = Normalize(range);
        return normalized.m_mipSliceMin == 0
            && normalized.m_mipSliceMax == m_mipLevels - 1
            && normalized.m_arraySliceMin == 0
            && normalized.m_arraySliceMax == m_arraySize - 1
            && normalized.m_aspectFlags == m_aspects;
    }

    void ImageSubresourceStates::GetSpans(const ImageSubresourceRange& range, SpanList& spans) const
    {
        spans.clear();

        const ImageSubresourceRange normalized = Normalize(range);
        if (IsUniform())
        {
            spans.push_back(ImageSubresourceSpan{ normalized, m_uniform });
            return;
        }

        const uint32_t planeCount = GetPlaneCount();
        for (uint32_t plane = 0; plane < planeCount; ++plane)
        {
            const ImageAspectFlags aspect = GetPlaneAspect(plane);
            if (!CheckBitsAny(normalized.m_aspectFlags, aspect))
            {
                continue;
            }

            for (uint32_t arraySlice = normalized.m_arraySliceMin; arraySlice <= normalized.m_arraySliceMax; ++arraySlice)
            {
                uint32_t runBegin = normalized.m_mipSliceMin;
                while (runBegin <= normalized.m_mipSliceMax)
                {
                    const ResourceState state = m_states[GetIndex(runBegin, arraySlice, plane)];
                    uint32_t runEnd = runBegin;
                    while (runEnd < normalized.m_mipSliceMax && m_states[GetIndex(runEnd + 1, arraySlice, plane)] == state)
                    {
                        ++runEnd;
                    }

                    // The slice before takes this run in when it split its mips the same way.
                    auto previous = eastl::find_if(spans.begin(), spans.end(), [&](const ImageSubresourceSpan& span)
                    {
                        return span.m_range.m_aspectFlags == aspect
                            && span.m_range.m_arraySliceMax + 1u == arraySlice
                            && span.m_range.m_mipSliceMin == runBegin
                            && span.m_range.m_mipSliceMax == runEnd
                            && span.m_state == state;
                    });
                    if (previous != spans.end())
                    {
                        previous->m_range.m_arraySliceMax = static_cast<uint16_t>(arraySlice);
                    }
                    else
                    {
                        ImageSubresourceSpan span;
                        span.m_range = ImageSubresourceRange(
                            static_cast<uint16_t>(runBegin), static_cast<uint16_t>(runEnd),
                            static_cast<uint16_t>(arraySlice), static_cast<uint16_t>(arraySlice));
                        span.m_range.m_aspectFlags = aspect;
                        span.m_state = state;
                        spans.push_back(span);
                    }

                    runBegin = runEnd + 1;
                }
            }
        }

        // Planes that split the same way go out as one span.
        for (size_t i = 0; i < spans.size(); ++i)
        {
            for (size_t j = i + 1; j < spans.size();)
            {
                if (spans[i].m_state == spans[j].m_state && CoversSameSlices(spans[i].m_range, spans[j].m_range))
                {
                    spans[i].m_range.m_aspectFlags |= spans[j].m_range.m_aspectFlags;
                    spans.erase(spans.begin() + j);
                }
                else
                {
                    ++j;
                }
            }
        }
    }

    void ImageSubresourceStates::Set(const ImageSubresourceRange& range, ResourceState state)
    {
        const ImageSubresourceRange normalized = Normalize(range);
        if (IsWholeImage(normalized))
        {
            SetUniform(state);
            return;
        }

        const uint32_t planeCount = GetPlaneCount();
        if (IsUniform())
        {
            if (m_uniform == state)
            {
                return;
            }
            m_states.assign(m_mipLevels * m_arraySize * planeCount, m_uniform);
        }

        for (uint32_t plane = 0; plane < planeCount; ++plane)
        {
            if (!CheckBitsAny(normalized.m_aspectFlags, GetPlaneAspect(plane)))
            {
                continue;
            }
            for (uint32_t arraySlice = normalized.m_arraySliceMin; arraySlice <= normalized.m_arraySliceMax; ++arraySlice)
            {
                for (uint32_t mip = normalized.m_mipSliceMin; mip <= normalized.m_mipSliceMax; ++mip)
                {
                    m_states[GetIndex(mip, arraySlice, plane)] = state;
                }
            }
        }

        const ResourceState first = m_states.front();
        const bool agree = eastl::all_of(m_states.begin(), m_states.end(), [&](const ResourceState& other)
        {
            return other == first;
        });
        if (agree)
        {
            SetUniform(first);
        }
    }

    uint32_t ImageSubresourceStates::GetPlaneCount() const
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < ImageAspectCount; ++i)
        {
            if (CheckBitsAny(m_aspects, AspectFlag(i)))
            {
                ++count;
            }
        }
        return count;
    }

    ImageAspectFlags ImageSubresourceStates::GetPlaneAspect(uint32_t plane) const
    {
        for (uint32_t i = 0; i < ImageAspectCount; ++i)
        {
            if (!CheckBitsAny(m_aspects, AspectFlag(i)))
            {
                continue;
            }
            if (plane == 0)
            {
                return AspectFlag(i);
            }
            --plane;
        }
        ASSERT(false, "[ImageSubresourceStates] Plane index is outside the image's aspects.");
        return ImageAspectFlags::None;
    }

    uint32_t ImageSubresourceStates::GetIndex(uint32_t mip, uint32_t arraySlice, uint32_t plane) const
    {
        return mip + arraySlice * m_mipLevels + plane * m_mipLevels * m_arraySize;
    }
}
