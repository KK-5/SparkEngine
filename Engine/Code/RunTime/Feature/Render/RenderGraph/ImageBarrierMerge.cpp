#include "ImageBarrierMerge.h"

#include <Log/ILogSystem.h>

namespace Spark::Render
{
    void AccumulateImageAccess(
        RHI::ImageSubresourceStates&      requests,
        const RHI::ImageSubresourceRange& range,
        RHI::AccessFlags                  access,
        RHI::AttachmentStage              stage)
    {
        ASSERT(access != RHI::AccessFlags::None, "[ImageBarrierMerge] An attachment asks for no access.");

        RHI::ImageSubresourceStates::SpanList spans;
        requests.GetSpans(range, spans);

        for (const RHI::ImageSubresourceSpan& span : spans)
        {
            RHI::ResourceState merged = span.m_state;
            merged.m_access |= access;
            merged.m_stage  |= stage;
            requests.Set(span.m_range, merged);
        }
    }

    void CollectImageTransitions(
        const RHI::ImageSubresourceStates& current,
        const RHI::ImageSubresourceStates& requests,
        ImageTransitionList&               transitions)
    {
        transitions.clear();

        RHI::ImageSubresourceStates::SpanList asked;
        requests.GetSpans(RHI::ImageSubresourceRange(), asked);

        RHI::ImageSubresourceStates::SpanList held;
        for (const RHI::ImageSubresourceSpan& request : asked)
        {
            if (request.m_state.m_access == RHI::AccessFlags::None)
            {
                continue;
            }

            current.GetSpans(request.m_range, held);
            for (const RHI::ImageSubresourceSpan& span : held)
            {
                transitions.push_back(ImageTransition{ span.m_range, span.m_state, request.m_state });
            }
        }
    }
}
