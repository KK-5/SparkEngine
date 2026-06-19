#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class UIProcessFeature final
    {
    public:
        void Process();

    private:
        //! Produce the CopyFrameBufferPass's CopyRequest. The request entity is
        //! created once (tagged with the pass); on later calls only its parameters
        //! are refreshed — the destination origin tracks the UI framebuffer pos.
        //! The copy itself (source/dest images, size) is resolved by the pass's
        //! default Compile from the declared attachment slots.
        void BuildCopyFrameBufferPassRequest(RHI::RHIContext& rhiCtx);

        RHI::RHIHandle m_copyRequestEntity = RHI::NullHandle;
    };
}
