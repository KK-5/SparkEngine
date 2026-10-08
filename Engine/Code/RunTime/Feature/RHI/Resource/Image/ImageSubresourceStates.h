#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/vector.h>

#include <RHI/Resource/ResourceState.h>
#include "ImageSubResource.h"

namespace Spark::RHI
{
    //! Subresources of one image sharing a state: a mip x array slice x aspect rectangle,
    //! which is what a backend barrier's subresource range covers.
    struct ImageSubresourceSpan
    {
        ImageSubresourceRange m_range;
        ResourceState         m_state;
    };

    //! The state of every subresource (mip x array slice x aspect) of an image. Holds a
    //! single state while they all agree, which is every image never accessed in part.
    class ImageSubresourceStates
    {
    public:
        using SpanList = eastl::fixed_vector<ImageSubresourceSpan, 2, true>;

        ImageSubresourceStates() = default;
        ImageSubresourceStates(
            uint16_t mipLevels, uint16_t arraySize, ImageAspectFlags aspects, ResourceState state = {});

        bool IsUniform() const;

        //! The state of the whole image. Asserts when its subresources differ.
        ResourceState GetUniform() const;
        void SetUniform(ResourceState state);

        //! Resolves HighestSliceIndex and ImageAspectFlags::All against the image's shape.
        ImageSubresourceRange Normalize(const ImageSubresourceRange& range) const;
        bool IsWholeImage(const ImageSubresourceRange& range) const;

        //! The states within the range, merged into as few rectangles as adjacent equal
        //! states allow.
        void GetSpans(const ImageSubresourceRange& range, SpanList& spans) const;

        //! Folds back into a single state once every subresource agrees again.
        void Set(const ImageSubresourceRange& range, ResourceState state);

    private:
        uint32_t GetPlaneCount() const;
        ImageAspectFlags GetPlaneAspect(uint32_t plane) const;
        uint32_t GetIndex(uint32_t mip, uint32_t arraySlice, uint32_t plane) const;

        uint16_t         m_mipLevels = 1;
        uint16_t         m_arraySize = 1;
        ImageAspectFlags m_aspects   = ImageAspectFlags::Color;

        ResourceState m_uniform;
        //! Empty while uniform. Indexed mip + arraySlice * mipLevels + plane * mipLevels * arraySize.
        eastl::vector<ResourceState> m_states;
    };
}
