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
        eastl::function<void()> m_compileFunction;
        eastl::function<void(RHI::CommandList* commandList)> m_executeFunction;
    };
}