#pragma once

#include <Base.h>
#include <Object/ObjectName.h>

#include <RHI/Resource/ShaderResource/InputStreamLayout.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>

#include <Resource/Shader/ShaderAsset.h>

#include <Pass/Pass.h>
#include <Pass/RHIContext.h>
#include <Pass/Component/RHIComponents.h>

namespace Spark::RHI
{
    class CommandList;
}

namespace Spark::Render
{
    struct RenderPassTag
    {
    };

    struct ComputePassTag
    {
    };

    struct RayTracingPassTag
    {
    };

    struct ActivePassTag
    {
    };
    
    ////////////////////////////////////////////////////
    struct PassName
    {
        ObjectName m_name {};
    };

    struct PassShaders
    {
        Ptr<Resource::ShaderAsset> m_vertexShader   = nullptr;
        Ptr<Resource::ShaderAsset> m_fragmentShader = nullptr;
        Ptr<Resource::ShaderAsset> m_geometryShader = nullptr;
        Ptr<Resource::ShaderAsset> m_computeShader  = nullptr;
    };

    struct PassShaderInputs
    {
        eastl::vector<RHI::ShaderInputBufferDescriptor>   m_inputBuffers;
        eastl::vector<RHI::ShaderInputImageDescriptor>    m_inputImages;
        eastl::vector<RHI::ShaderInputSamplerDescriptor>  m_inputSamplers;
        eastl::vector<RHI::ShaderInputConstantDescriptor> m_inputConstants;
    };

    struct PassFunctions
    {
        eastl::function<void(RHIContext& rhiContext)> m_buildFunction;
        eastl::function<void(RHIContext& rhiContext)> m_compileFunction;
        eastl::function<void(RHI::CommandList* commandList)> m_executeFunction;
    };

    // Compiled barriers for a single pass. Filled by CompileImageBarriers /
    // CompileBufferBarriers, consumed by execute. Per-frame, cleared at frame end.
    struct PassBarriers
    {
        eastl::vector<RHI::ImageBarrier>  m_preImage;
        eastl::vector<RHI::BufferBarrier> m_preBuffer;
        eastl::vector<RHI::ImageBarrier>  m_postImage;
        eastl::vector<RHI::BufferBarrier> m_postBuffer;
    };

    // Lives on a Pass entity. Engine invokes m_markFn during the compile phase to
    // tag this pass's PassAttachments with AttachmentCompilingTag, so engine-level
    // barrier compilation can iterate them without scanning all attachments.
    struct PassAttachmentMarker
    {
        void (*m_markFn)(RHIContext&) = nullptr;
    };

    template <typename PassTagT>
    PassAttachmentMarker MarkPassAttachmentCompiling()
    {
        PassAttachmentMarker result;

        result.m_markFn = [](RHIContext& ctx)
        {
            ctx.GetView<ImagePassAttachment, PassTagT>().each(
                [&ctx](auto handle, const ImagePassAttachment&)
                {
                    ctx.Add<AttachmentCompilingTag>(handle);
                });

            ctx.GetView<BufferPassAttachment, PassTagT>().each(
                [&ctx](auto handle, const BufferPassAttachment&)
                {
                    ctx.Add<AttachmentCompilingTag>(handle);
                });
        };

        return result;
    }
}