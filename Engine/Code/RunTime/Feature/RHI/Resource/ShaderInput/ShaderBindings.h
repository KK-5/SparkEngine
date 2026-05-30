#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/Device/DeviceObject.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>

namespace Spark::RHI
{
    /**
     * Runtime binding data for a single ShaderInputGroup (one HLSL space).
     *
     * Created from a PipelineLayoutDescriptor + HLSL space id. At Init time, the descriptor
     * metadata is extracted from the layout and a ShaderInputXxx object is created for
     * each handle in the space group. Users then look up individual inputs by name to set
     * bound resource data (views, sampler states, constant bytes).
     *
     * At compile time, the backend ShaderInputCompiler reads the ShaderInput data and
     * produces backend-specific binding commands (descriptor table writes, CBV addresses).
     * The compile output is stored in the backend-specific derived class, keeping per-space
     * compile results naturally partitioned for multi-threaded compile.
     */
    class ShaderBindings : public DeviceObject
    {
        friend class Factory;

    public:
        struct Descriptor
        {
            ConstPtr<PipelineLayoutDescriptor> m_layout;
            uint32_t                           m_spaceId = 0;  // HLSL space number, not m_spaceGroups[] array index
        };

        virtual ~ShaderBindings() = default;

        ResultCode Init(Device& device, const Descriptor& descriptor);
        void       Shutdown() override;

        /// Name-based lookup for setting bound resource data.
        /// Returns nullptr if no input with the given name exists in this space group.
        ShaderInputBuffer*   FindBufferInput(const InputName& name);
        ShaderInputImage*    FindImageInput(const InputName& name);
        ShaderInputSampler*  FindSamplerInput(const InputName& name);
        ShaderInputConstant* FindConstantInput(const InputName& name);

        const ShaderInputBuffer*   FindBufferInput(const InputName& name)   const;
        const ShaderInputImage*    FindImageInput(const InputName& name)    const;
        const ShaderInputSampler*  FindSamplerInput(const InputName& name)  const;
        const ShaderInputConstant* FindConstantInput(const InputName& name) const;

        /// Iteration over all inputs in this space group (for compile).
        eastl::span<const ShaderInputBuffer>   GetBufferInputs()   const { return { m_buffers.data(),   m_buffers.size() }; }
        eastl::span<const ShaderInputImage>    GetImageInputs()    const { return { m_images.data(),    m_images.size() }; }
        eastl::span<const ShaderInputSampler>  GetSamplerInputs()  const { return { m_samplers.data(),  m_samplers.size() }; }
        eastl::span<const ShaderInputConstant> GetConstantInputs() const { return { m_constants.data(), m_constants.size() }; }

        /// Metadata.
        uint32_t                              GetSpaceId()             const { return m_spaceId; }
        const PipelineLayoutDescriptor&       GetLayoutDescriptor()    const { return *m_layoutDescriptor; }

    protected:
        ShaderBindings() = default;

        virtual ResultCode InitInternal(Device& device, const Descriptor& descriptor) = 0;
        virtual void       ShutdownInternal() = 0;

    private:
        void BuildInputsFromLayout(const Descriptor& descriptor);

        ConstPtr<PipelineLayoutDescriptor> m_layoutDescriptor;
        uint32_t                           m_spaceId = 0;

        eastl::vector<ShaderInputBuffer>   m_buffers;
        eastl::vector<ShaderInputImage>    m_images;
        eastl::vector<ShaderInputSampler>  m_samplers;
        eastl::vector<ShaderInputConstant> m_constants;
    };

} // namespace Spark::RHI
