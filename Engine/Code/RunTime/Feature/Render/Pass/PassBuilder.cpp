#include "PassBuilder.h"

#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <Resource/Shader/ShaderBuilder.h>

namespace Spark::Render
{
    namespace
    {
        const RHI::ShaderBindings* FindGroup(
            eastl::span<const RHI::ShaderBindings* const> groups, uint32_t spaceId)
        {
            for (const RHI::ShaderBindings* group : groups)
            {
                if (group->GetSpaceId() == spaceId)
                {
                    return group;
                }
            }
            return nullptr;
        }

        //! The group's space, entry by entry in the group's own order: AddShaderInputDescriptors
        //! files them the way it did for the group, so the pass's table matches the group's.
        void AppendGroupSpace(const RHI::ShaderBindings& group, RHI::ShaderStageMask stages, RHI::ShaderInputList& out)
        {
            const RHI::PipelineLayoutDescriptor& layout = group.GetLayoutDescriptor();
            const RHI::ShaderInputGroup* space = layout.FindSpaceGroupBySpaceId(group.GetSpaceId());
            ASSERT(space, "[BuildPassPipelineLayout] The group at space{} has no such space in its layout.",
                group.GetSpaceId());

            for (const RHI::ShaderInputHandle& handle : space->m_shaderInputs)
            {
                switch (handle.m_type)
                {
                case RHI::ShaderInputType::Buffer:
                {
                    RHI::ShaderInputBufferDescriptor desc = layout.GetBufferDescriptor(handle.m_index);
                    desc.m_stageMask = stages;
                    out.m_buffers.push_back(desc);
                    break;
                }
                case RHI::ShaderInputType::Image:
                {
                    RHI::ShaderInputImageDescriptor desc = layout.GetImageDescriptor(handle.m_index);
                    desc.m_stageMask = stages;
                    out.m_images.push_back(desc);
                    break;
                }
                case RHI::ShaderInputType::Sampler:
                {
                    RHI::ShaderInputSamplerDescriptor desc = layout.GetSamplerDescriptor(handle.m_index);
                    desc.m_stageMask = stages;
                    out.m_samplers.push_back(desc);
                    break;
                }
                case RHI::ShaderInputType::Constant:
                {
                    RHI::ShaderInputConstantDescriptor desc = layout.GetConstantDescriptor(handle.m_index);
                    desc.m_stageMask = stages;
                    out.m_constants.push_back(desc);
                    break;
                }
                default:
                    break;
                }
            }
        }

        //! What a shader reads of a group must be what the group declares. The hash covers
        //! every field but the space, which the lookup by name already pins, and the stage mask,
        //! which the shader fills and the group leaves None.
        template<typename Descriptor>
        bool Matches(Descriptor read, const Descriptor& declared)
        {
            read.m_stageMask = declared.m_stageMask;
            return read.GetHash() == declared.GetHash();
        }

        const RHI::ShaderInputBufferDescriptor* FindDeclared(
            const RHI::PipelineLayoutDescriptor& layout, const RHI::ShaderInputBufferDescriptor& read)
        {
            return layout.FindBufferDescriptor(read.m_name);
        }

        const RHI::ShaderInputImageDescriptor* FindDeclared(
            const RHI::PipelineLayoutDescriptor& layout, const RHI::ShaderInputImageDescriptor& read)
        {
            return layout.FindImageDescriptor(read.m_name);
        }

        const RHI::ShaderInputSamplerDescriptor* FindDeclared(
            const RHI::PipelineLayoutDescriptor& layout, const RHI::ShaderInputSamplerDescriptor& read)
        {
            return layout.FindSamplerDescriptor(read.m_name);
        }

        const RHI::ShaderInputConstantDescriptor* FindDeclared(
            const RHI::PipelineLayoutDescriptor& layout, const RHI::ShaderInputConstantDescriptor& read)
        {
            return layout.FindConstantDescriptor(read.m_name);
        }

        //! One reflected input: the pass's own space keeps it, a bound group's space checks it
        //! against the group (its entry is already in from AppendGroupSpace), any other space
        //! is a group the pass does not bind.
        template<typename Descriptor>
        void TakeReflected(
            const eastl::vector<Descriptor>& reflected, eastl::vector<Descriptor>& out,
            eastl::span<const RHI::ShaderBindings* const> groups, const char* passName)
        {
            for (const Descriptor& read : reflected)
            {
                if (read.m_spaceId == kPerPassSpaceId)
                {
                    out.push_back(read);
                    continue;
                }

                const RHI::ShaderBindings* group = FindGroup(groups, read.m_spaceId);
                if (!group)
                {
                    LOG_ERROR("[BuildPassPipelineLayout] Pass '{}' reads '{}' in space{}, but binds no group there.",
                        passName, read.m_name.GetCStr(), read.m_spaceId);
                    continue;
                }

                const Descriptor* declared = FindDeclared(group->GetLayoutDescriptor(), read);
                if (!declared)
                {
                    LOG_ERROR("[BuildPassPipelineLayout] Pass '{}' reads '{}' in space{}, which the group there "
                        "does not declare.", passName, read.m_name.GetCStr(), read.m_spaceId);
                }
                else if (!Matches(read, *declared))
                {
                    LOG_ERROR("[BuildPassPipelineLayout] Pass '{}' reads '{}' in space{} unlike the group declares "
                        "it: register {} in the shader, {} in the group, or else another of its fields differs.",
                        passName, read.m_name.GetCStr(), read.m_spaceId, read.m_registerId, declared->m_registerId);
                }
            }
        }

        //! `stage` of `shader` as a stage function, or null when the pass sets no such shader.
        Ptr<RHI::ShaderStageFunction> CreateStageFunction(
            RHI::Factory& factory, const Ptr<Resource::ShaderAsset>& shader, RHI::ShaderStage stage,
            const char* passName, const char* stageName)
        {
            if (!shader)
            {
                return nullptr;
            }

            const auto* bytecode = shader->GetShaderData()->GetStageBytecode(stage);
            ASSERT(bytecode, "Pass '{}': its {} has no bytecode for that stage.", passName, stageName);
            auto function = factory.CreateShaderStageFunction(stage);
            function->SetByteCode(bytecode->bytecode);
            function->Finalize();
            return function;
        }
    }

    PassStageFunctions CreatePassStageFunctions(
        RHI::Factory& factory, const PassShaders& shaders, const char* passName)
    {
        PassStageFunctions functions;
        functions.m_vertex   = CreateStageFunction(factory, shaders.m_vertexShader, RHI::ShaderStage::Vertex, passName, "VertexShader");
        functions.m_geometry = CreateStageFunction(factory, shaders.m_geometryShader, RHI::ShaderStage::Geometry, passName, "GeometryShader");
        functions.m_fragment = CreateStageFunction(factory, shaders.m_fragmentShader, RHI::ShaderStage::Fragment, passName, "FragmentShader");
        functions.m_compute  = CreateStageFunction(factory, shaders.m_computeShader, RHI::ShaderStage::Compute, passName, "ComputeShader");
        return functions;
    }

    Ptr<RHI::PipelineLayoutDescriptor> BuildPassPipelineLayout(
        RHI::Factory& factory, const PassShaders& shaders,
        eastl::span<const RHI::ShaderBindings* const> groups, const char* passName)
    {
        const Resource::ShaderAsset* assets[] = {
            shaders.m_vertexShader.get(),
            shaders.m_fragmentShader.get(),
            shaders.m_geometryShader.get(),
            shaders.m_computeShader.get(),
        };
        Resource::ShaderInputBuildResult reflected = Resource::BuildShaderInputList(
            eastl::span<const Resource::ShaderAsset* const>(assets, 4));

        if (reflected.stageMask == RHI::ShaderStageMask::None)
        {
            return nullptr;
        }

        RHI::ShaderInputList inputs;
        for (const RHI::ShaderBindings* group : groups)
        {
            AppendGroupSpace(*group, reflected.stageMask, inputs);
        }

        TakeReflected(reflected.list.m_buffers,   inputs.m_buffers,   groups, passName);
        TakeReflected(reflected.list.m_images,    inputs.m_images,    groups, passName);
        TakeReflected(reflected.list.m_samplers,  inputs.m_samplers,  groups, passName);
        TakeReflected(reflected.list.m_constants, inputs.m_constants, groups, passName);
        inputs.m_rootConstants = eastl::move(reflected.list.m_rootConstants);

        auto layout = factory.CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(inputs);
        layout->Finalize();
        return layout;
    }
}
