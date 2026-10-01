#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Resource/Image/ImageSubresourceStates.h>

//! How one image's accesses within one Scope turn into barriers. Free of the ECS and of a
//! device, so it is tested on its own.
namespace Spark::Render
{
    //! ORs an access into every subresource of the range. `requests` has the image's shape; a
    //! subresource nothing asked for yet has no access. Accesses that conflict on a subresource
    //! are the builder's to reject (RenderGraphBuilder::CloseScope).
    void AccumulateImageAccess(
        RHI::ImageSubresourceStates&      requests,
        const RHI::ImageSubresourceRange& range,
        RHI::AccessFlags                  access,
        RHI::AttachmentStage              stage);

    //! Subresources going from one state to one other: a rectangle, as a barrier's range is.
    struct ImageTransition
    {
        RHI::ImageSubresourceRange m_range;
        RHI::ResourceState         m_src;
        RHI::ResourceState         m_dst;
    };

    using ImageTransitionList = eastl::fixed_vector<ImageTransition, 2, true>;

    //! What takes the subresources that were asked for from where `current` has them to where
    //! `requests` wants them. Whether a transition needs a barrier is left to the caller.
    void CollectImageTransitions(
        const RHI::ImageSubresourceStates& current,
        const RHI::ImageSubresourceStates& requests,
        ImageTransitionList&               transitions);
}
