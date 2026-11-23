#pragma once

#include <Base.h>
#include <EASTL/array.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Format.h>

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::Render
{
    class PassContext;

    namespace NRDInputs
    {
        inline const RHI::AttachmentId Diffuse("NRDInputDiffuse");
        inline const RHI::AttachmentId Specular("NRDInputSpecular");
        inline const RHI::AttachmentId NormalRoughness("NRDInputNormalRoughness");
        inline const RHI::AttachmentId Motion("NRDMotion");
        inline const RHI::AttachmentId HistoryConfidence("NRDHistoryConfidence");
    }

    struct NRDInputPass
    {
        static Ptr<Resource::ShaderAsset> LoadShader();
        static void SetUp(PassContext& ctx, const Ptr<Resource::ShaderAsset>& shader,
            RHI::Format normalRoughnessFormat, const eastl::array<float, 3>& hitDistanceParameters);
    };
}
