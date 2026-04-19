#pragma once

#include <Base.h>

#include <RHI/Resource/ShaderResource/InputStreamLayout.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>

#include <Resource/Shader/ShaderAsset.h>

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
    
    ////////////////////////////////////////////////////
    struct PassInputLayout
    {
        RHI::InputStreamLayout m_inputLayout {};
    };

    struct PassRenderAttachment
    {
        RHI::RenderAttachmentConfiguration m_config {};
    };

    struct PassRenderStates
    {
        RHI::RenderStates m_states {};
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
        eastl::function<void()> m_buildFunction;
        eastl::function<void()> m_compileFunction;
        eastl::function<void()> m_executeFunction;
    };
}