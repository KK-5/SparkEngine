#include "PassBuilder.h"

#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedGroupLayouts.h>
#include <Binding/SharedBindings.h>

namespace Spark::Render
{
    namespace
    {
        //! The group's space, entry by entry in the group's own order: AddShaderInputDescriptors
        //! files them the way the declaration lays them out, so the pass's table matches the
        //! group's.
        void AppendSharedGroupSpace(
            const RHI::PipelineLayoutDescriptor& layout, uint32_t spaceId,
            RHI::ShaderStageMask stages, RHI::ShaderInputList& out)
        {
            const RHI::ShaderInputGroup* space = layout.FindSpaceGroupBySpaceId(spaceId);
            ASSERT(space, "[BuildPassPipelineLayout] The declaration for space{} has no such space in its layout.",
                spaceId);

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
                case RHI::ShaderInputType::AccelerationStructure:
                {
                    RHI::ShaderInputAccelerationStructureDescriptor desc =
                        layout.GetAccelerationStructureDescriptor(handle.m_index);
                    desc.m_stageMask = stages;
                    out.m_accelerationStructures.push_back(desc);
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

        const RHI::ShaderInputAccelerationStructureDescriptor* FindDeclared(
            const RHI::PipelineLayoutDescriptor& layout, const RHI::ShaderInputAccelerationStructureDescriptor& read)
        {
            return layout.FindAccelerationStructureDescriptor(read.m_name);
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

        //! One reflected input: the pass's own space keeps it; a shared space checks it against
        //! the graph's declaration for that space (its entry is already in from
        //! AppendSharedGroupSpace).
        template<typename Descriptor>
        void TakeReflected(
            const eastl::vector<Descriptor>& reflected, eastl::vector<Descriptor>& out,
            RHI::Factory& factory, const char* passName)
        {
            for (const Descriptor& read : reflected)
            {
                if (read.m_spaceId == kPerPassSpaceId)
                {
                    out.push_back(read);
                    continue;
                }

                const RHI::PipelineLayoutDescriptor* declaration = FindSharedGroupLayout(factory, read.m_spaceId);
                if (!declaration)
                {
                    LOG_ERROR("[BuildPassPipelineLayout] Pass '{}' reads '{}' in space{}, and no group declares "
                        "that space.", passName, read.m_name.GetCStr(), read.m_spaceId);
                    continue;
                }

                const Descriptor* declared = FindDeclared(*declaration, read);
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
        RHI::Factory& factory, const PassShaders& shaders, const char* passName)
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

        // What a pass uses is its shaders' reflection: a shader reading a shared space names it
        // in its registers, so the declaration that fills that space is found by the space. No
        // group object is involved: a layout comes from the graph's declarations.
        RHI::ShaderInputList inputs;
        eastl::fixed_vector<uint32_t, RHI::Limits::Pipeline::ShaderInputGroupCountMax> resolvedSpaces;
        const auto takeSpaceOf = [&](uint32_t spaceId)
        {
            if (spaceId == kPerPassSpaceId || spaceId == kPerScopeSpaceId
                || eastl::find(resolvedSpaces.begin(), resolvedSpaces.end(), spaceId) != resolvedSpaces.end())
            {
                return;
            }

            // No declaration there is TakeReflected's to report: it knows which input asked.
            const RHI::PipelineLayoutDescriptor* declaration = FindSharedGroupLayout(factory, spaceId);
            if (declaration == nullptr)
            {
                return;
            }

            AppendSharedGroupSpace(*declaration, spaceId, reflected.stageMask, inputs);
            resolvedSpaces.push_back(spaceId);
        };
        const auto takeSpacesOf = [&takeSpaceOf](const auto& described)
        {
            for (const auto& input : described)
            {
                takeSpaceOf(input.m_spaceId);
            }
        };
        takeSpacesOf(reflected.list.m_buffers);
        takeSpacesOf(reflected.list.m_images);
        takeSpacesOf(reflected.list.m_accelerationStructures);
        takeSpacesOf(reflected.list.m_samplers);
        takeSpacesOf(reflected.list.m_constants);

        TakeReflected(reflected.list.m_buffers,   inputs.m_buffers,   factory, passName);
        TakeReflected(reflected.list.m_images,    inputs.m_images,    factory, passName);
        TakeReflected(reflected.list.m_accelerationStructures, inputs.m_accelerationStructures, factory, passName);
        TakeReflected(reflected.list.m_samplers,  inputs.m_samplers,  factory, passName);
        TakeReflected(reflected.list.m_constants, inputs.m_constants, factory, passName);
        inputs.m_rootConstants = eastl::move(reflected.list.m_rootConstants);

        auto layout = factory.CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(inputs);
        layout->Finalize();
        return layout;
    }
}
