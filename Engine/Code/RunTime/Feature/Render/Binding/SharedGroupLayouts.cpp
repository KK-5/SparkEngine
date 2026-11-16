#include "SharedGroupLayouts.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <RHI/Factory.h>

#include <Binding/BindingSpaces.h>

namespace Spark::Render
{
    namespace
    {
        // Scene (space0), the layout of SceneBindings.hlsli.

        //! The scene's buffers and images, by register: the order the group lays them out.
        struct SceneBufferInput
        {
            const char* m_name;
            uint32_t    m_register;
        };
        struct SceneImageInput
        {
            const char*               m_name;
            RHI::ShaderInputImageType m_type;
            uint32_t                  m_register;
        };
        constexpr SceneBufferInput SceneBuffers[] = {
            { SharedInput::Lights,         0 },
            { SharedInput::ShadowViews,    4 },
            { SharedInput::ViewShadowMaps, 7 },
        };
        constexpr SceneImageInput SceneImages[] = {
            { SharedInput::IrradianceCube,  RHI::ShaderInputImageType::ImageCube, 1 },
            { SharedInput::PrefilteredCube, RHI::ShaderInputImageType::ImageCube, 2 },
            { SharedInput::BRDFLut,         RHI::ShaderInputImageType::Image2D,   3 },
            { SharedInput::LTC1,            RHI::ShaderInputImageType::Image2D,   5 },
            { SharedInput::LTC2,            RHI::ShaderInputImageType::Image2D,   6 },
        };

        RHI::ShaderInputList BuildSceneInputs()
        {
            RHI::ShaderInputList inputs;
            for (const SceneBufferInput& buffer : SceneBuffers)
            {
                inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                    RHI::InputName(buffer.m_name), RHI::ShaderInputBufferAccess::Read,
                    RHI::ShaderInputBufferType::Structured, 1, 0, buffer.m_register, kSceneSpaceId));
            }
            for (const SceneImageInput& image : SceneImages)
            {
                inputs.m_images.push_back(RHI::ShaderInputImageDescriptor(
                    RHI::InputName(image.m_name), RHI::ShaderInputImageAccess::Read,
                    image.m_type, 1, image.m_register, kSceneSpaceId));
            }
            inputs.m_samplers.push_back(
                RHI::ShaderInputSamplerDescriptor(RHI::InputName(SharedInput::IBLSampler), 1, 0, kSceneSpaceId));

            // cbuffer SceneConstants : register(b0), every member a 4-byte scalar.
            uint32_t constantOffset = 0;
            for (const char* name : { SharedInput::LightCount, SharedInput::PrefilteredMips, SharedInput::EnvIntensity,
                                      SharedInput::ShadowTexelSize, SharedInput::FrameNumber, SharedInput::GameTime,
                                      SharedInput::PrevGameTime, SharedInput::DeltaTime, SharedInput::AreaLightLutReady })
            {
                inputs.m_constants.push_back(RHI::ShaderInputConstantDescriptor(
                    RHI::InputName(name), constantOffset, 4, 0, kSceneSpaceId));
                constantOffset += 4;
            }
            return inputs;
        }

        RHI::ShaderInputList BuildViewInputs()
        {
            RHI::ShaderInputList inputs;
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(SharedInput::Views), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Structured, 1, 0, 0, kViewSpaceId));
            return inputs;
        }

        RHI::ShaderInputList BuildMaterialInputs()
        {
            RHI::ShaderInputList inputs;
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(SharedInput::Materials), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Structured, 1, 0, 0, kMaterialSpaceId));
            return inputs;
        }

        RHI::ShaderInputList BuildInstanceInputs()
        {
            RHI::ShaderInputList inputs;
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(SharedInput::Instances), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Structured, 1, 0, 0, kInstanceSpaceId));
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(SharedInput::EmissiveInstanceIDs), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Structured, 1, 0, 1, kInstanceSpaceId));
            return inputs;
        }

        RHI::ShaderInputList BuildGeometryInputs()
        {
            RHI::ShaderInputList inputs;
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(SharedInput::Geometries), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Structured, 1, 0, 0, kGeometrySpaceId));
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(SharedInput::MeshGeometry), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Raw, 1, 0, 1, kGeometrySpaceId));
            return inputs;
        }

        RHI::ShaderInputList BuildRayTracingInputs()
        {
            RHI::ShaderInputList inputs;
            inputs.m_accelerationStructures.push_back(
                RHI::ShaderInputAccelerationStructureDescriptor(
                    RHI::InputName(SharedInput::RayTracingScene), 1, 0, kRayTracingSpaceId));
            return inputs;
        }

        //! One shared group: the space it lives in, the inputs it holds, and the layout built
        //! from them the first time a pass asks for it. Plain CPU data, no device resources.
        struct SharedGroup
        {
            uint32_t             m_spaceId = 0;
            RHI::ShaderInputList m_inputs;
            ConstPtr<RHI::PipelineLayoutDescriptor> m_layout;
        };

        //! The graph's shared groups, one per space, built the first time anything asks.
        eastl::fixed_vector<SharedGroup, RHI::Limits::Pipeline::ShaderInputGroupCountMax>& SharedGroups()
        {
            static eastl::fixed_vector<SharedGroup, RHI::Limits::Pipeline::ShaderInputGroupCountMax> groups;
            if (!groups.empty())
            {
                return groups;
            }

            groups.push_back(SharedGroup{ kSceneSpaceId, BuildSceneInputs(), nullptr });
            groups.push_back(SharedGroup{ kViewSpaceId, BuildViewInputs(), nullptr });
            groups.push_back(SharedGroup{ kMaterialSpaceId, BuildMaterialInputs(), nullptr });
            groups.push_back(SharedGroup{ kInstanceSpaceId, BuildInstanceInputs(), nullptr });
            groups.push_back(SharedGroup{ kGeometrySpaceId, BuildGeometryInputs(), nullptr });
            groups.push_back(SharedGroup{ kRayTracingSpaceId, BuildRayTracingInputs(), nullptr });
            return groups;
        }

        SharedGroup* FindSharedGroup(uint32_t spaceId)
        {
            for (SharedGroup& group : SharedGroups())
            {
                if (group.m_spaceId == spaceId)
                {
                    return &group;
                }
            }
            return nullptr;
        }
    }

    const RHI::ShaderInputList* FindSharedGroupInputs(uint32_t spaceId)
    {
        const SharedGroup* group = FindSharedGroup(spaceId);
        return group != nullptr ? &group->m_inputs : nullptr;
    }

    const RHI::PipelineLayoutDescriptor* FindSharedGroupLayout(RHI::Factory& factory, uint32_t spaceId)
    {
        SharedGroup* group = FindSharedGroup(spaceId);
        if (group == nullptr)
        {
            return nullptr;
        }
        if (!group->m_layout)
        {
            auto layout = factory.CreatePipelineLayoutDescriptor();
            layout->AddShaderInputDescriptors(group->m_inputs);
            layout->Finalize();
            group->m_layout = eastl::move(layout);
        }
        return group->m_layout.get();
    }
}
