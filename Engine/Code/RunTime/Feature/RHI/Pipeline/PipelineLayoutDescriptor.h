#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <Base.h>
#include <Object/Object.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/ShaderResource/ConstantsLayout.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>
#include "ShaderStages.h"

namespace Spark::RHI
{
    //! 引用 PipelineLayoutDescriptor 各类型 descriptor 数组里的一条记录。
    //! m_index 是对应类型数组（m_bufferDescs / m_imageDescs / ...）里的下标。
    struct ShaderInputHandle
    {
        ShaderInputType m_type  = ShaderInputType::Count;
        uint32_t        m_index = 0;
    };

    //! Aggregated CBV slot — one entry per unique HLSL cbuffer register within a space.
    //! All ShaderInputConstants sharing the same m_registerId belong to this slot and
    //! are packed into a single backend constant buffer (DX12 root CBV, Vulkan UBO).
    //! Computed by PipelineLayoutDescriptor::Finalize so both PipelineLayout (root sig /
    //! descriptor set layout) and ShaderInputCompiler (memory allocation) consume the
    //! same precomputed layout.
    struct ConstantBufferLayout
    {
        uint32_t m_registerId = 0;
        uint32_t m_byteSize   = 0;  // 256B-aligned; covers all constants at this register
        uint32_t m_byteOffset = 0;  // prefix-sum byte offset within the space's per-frame slice
    };

    //! 同一 HLSL space 内所有 ShaderInput descriptor 的集合。
    //! 对应 DX12 的一个（或两个，view/sampler heap 分离时）root parameter，
    //! 对应 Vulkan 的一个 VkDescriptorSet（set = m_spaceId）。
    struct ShaderInputGroup
    {
        static constexpr uint32_t ConstantBufferCountMax = 8;

        uint32_t        m_spaceId   = 0;
        ShaderStageMask m_stageMask = ShaderStageMask::None;
        eastl::vector<ShaderInputHandle> m_shaderInputs;

        //! Filled at Finalize. Order = first-seen order of m_registerId among Constant
        //! handles in m_shaderInputs. Both backends iterate this in lock-step with their
        //! own CBV index k.
        eastl::fixed_vector<ConstantBufferLayout, ConstantBufferCountMax> m_constantBuffers;

        uint32_t GetConstantBytesPerFrame() const
        {
            return m_constantBuffers.empty()
                ? 0u
                : (m_constantBuffers.back().m_byteOffset + m_constantBuffers.back().m_byteSize);
        }
    };

    //! ShaderAsset 反射产出的临时容器，通过 AddShaderInputDescriptors() 消费后销毁。
    //! 只持有纯 layout descriptor，不含任何资源数据。
    struct ShaderInputList
    {
        eastl::vector<ShaderInputBufferDescriptor>   m_buffers;
        eastl::vector<ShaderInputImageDescriptor>    m_images;
        eastl::vector<ShaderInputSamplerDescriptor>  m_samplers;
        eastl::vector<ShaderInputConstantDescriptor> m_constants;
    };

    //=========================================================================
    // PipelineLayoutDescriptor
    //=========================================================================

    class PipelineLayoutDescriptor : public Object
    {
        friend class Factory;
    public:
        virtual ~PipelineLayoutDescriptor() noexcept = default;

        bool       IsFinalized() const;
        void       Reset();
        ResultCode Finalize();
        size_t     GetHash() const;

        //---------------------------------------------------------------------
        // Root constants（push constant）
        //---------------------------------------------------------------------

        void SetRootConstantsLayout(const ConstantsLayout& rootConstantsLayout);

        const ConstantsLayout* GetRootConstantsLayout() const;

        //---------------------------------------------------------------------
        // ShaderInput API
        //---------------------------------------------------------------------

        //! 批量添加一组 descriptor，stageMask 指明使用这组 input 的着色器阶段。
        //! 内部按 spaceId 自动分组到 m_spaceGroups，并对同 space 同类型的
        //! register 重叠做 Validation 检查。
        void AddShaderInputDescriptors(const ShaderInputList& list, ShaderStageMask stageMask);

        //! 添加一个 static sampler，不进入 SpaceGroup（DX12 static sampler 不占 root parameter）。
        void AddStaticSamplerDescriptor(
            const ShaderInputStaticSamplerDescriptor& desc,
            ShaderStageMask stageMask);

        // SpaceGroup 访问 — DX12 build loop 按数组下标遍历用
        size_t                  GetSpaceGroupCount() const;
        const ShaderInputGroup& GetSpaceGroup(size_t index) const;

        // 按 HLSL space 号查 group — ShaderBindings::Init 等按 spaceId 定位用。
        // 返回 nullptr 表示该 spaceId 没有对应 group。线性扫描，fixed_vector ≤8 项。
        const ShaderInputGroup* FindSpaceGroupBySpaceId(uint32_t spaceId) const;

        // 按下标取单个 descriptor — 配合 ShaderInputRef 在 SpaceGroup loop 里使用
        const ShaderInputBufferDescriptor&        GetBufferDescriptor(uint32_t index) const;
        const ShaderInputImageDescriptor&         GetImageDescriptor(uint32_t index) const;
        const ShaderInputSamplerDescriptor&       GetSamplerDescriptor(uint32_t index) const;
        const ShaderInputConstantDescriptor&      GetConstantDescriptor(uint32_t index) const;

        // Static sampler 访问 — DX12 build loop 在 root signature 末尾处理
        size_t                                    GetStaticSamplerCount() const;
        const ShaderInputStaticSamplerDescriptor& GetStaticSamplerDescriptor(uint32_t index) const;
        ShaderStageMask                           GetStaticSamplerStageMask(uint32_t index) const;

        // 名字查找 — 调试/校验用，线性扫描，不在热路径上调用
        const ShaderInputBufferDescriptor*   FindBufferDescriptor(const InputName& name) const;
        const ShaderInputImageDescriptor*    FindImageDescriptor(const InputName& name) const;
        const ShaderInputSamplerDescriptor*  FindSamplerDescriptor(const InputName& name) const;
        const ShaderInputConstantDescriptor* FindConstantDescriptor(const InputName& name) const;

    protected:
        PipelineLayoutDescriptor() = default;

    private:
        //---------------------------------------------------------------------
        // Platform virtuals
        //---------------------------------------------------------------------
        virtual void       ResetInternal();
        virtual ResultCode FinalizeInternal();
        virtual size_t     GetHashInternal(size_t seed) const;

        //! Override in backend to check whether two ShaderInput handles have overlapping
        //! bindings within the same space. Called once per (newHandle, existing) pair
        //! inside InsertShaderInput under Validation::isEnabled.
        //! Default: no-op — backends provide the API-specific implementation.
        virtual void ValidateShaderInputOverlapInternal(
            const ShaderInputHandle& newHandle,
            const ShaderInputHandle& existingHandle,
            uint32_t spaceId) const;

        //! SpaceGroup 分组逻辑：找到已有的同 spaceId group 或新建，做 register 重叠检查。
        void InsertShaderInput(ShaderInputHandle handle, uint32_t spaceId, ShaderStageMask stageMask);

        //! Finalize-time pass: per SpaceGroup, dedup Constant handles by m_registerId,
        //! compute aligned byteSize and prefix-sum byteOffset for each unique register.
        //! Output: ShaderInputGroup::m_constantBuffers.
        void BuildConstantBufferLayouts();

        static constexpr size_t InvalidHash    = static_cast<size_t>(~0);

        //---------------------------------------------------------------------
        // Root constants（push constant）
        //---------------------------------------------------------------------
        Ptr<ConstantsLayout> m_rootConstantsLayout;

        //---------------------------------------------------------------------
        // ShaderInput 数据
        //---------------------------------------------------------------------
        eastl::vector<ShaderInputBufferDescriptor>        m_bufferDescs;
        eastl::vector<ShaderInputImageDescriptor>         m_imageDescs;
        eastl::vector<ShaderInputSamplerDescriptor>       m_samplerDescs;
        eastl::vector<ShaderInputConstantDescriptor>      m_constantDescs;
        struct StaticSamplerEntry
        {
            ShaderInputStaticSamplerDescriptor m_desc;
            ShaderStageMask                    m_stageMask = ShaderStageMask::None;
        };
        eastl::vector<StaticSamplerEntry> m_staticSamplerDescs;

        eastl::fixed_vector<ShaderInputGroup, Limits::Pipeline::ShaderInputGroupCountMax> m_spaceGroups;

        size_t m_hash = InvalidHash;
    };

} // namespace Spark::RHI
