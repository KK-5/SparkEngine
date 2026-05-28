#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/array.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <Base.h>
#include <Object/Object.h>

#include <RHI/RHILimits.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/ShaderResource/ConstantsLayout.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>
#include "ShaderStages.h"

namespace Spark::RHI
{
    //=========================================================================
    // 旧路径（SRG）binding structures — 保持不动，等新路径稳定后再删
    //=========================================================================

    struct ResourceBindingInfo
    {
        ResourceBindingInfo() = default;
        ResourceBindingInfo(const RHI::ShaderStageMask& mask, uint32_t registerId, uint32_t spaceId)
            : m_shaderStageMask{ mask }
            , m_registerId{ registerId }
            , m_spaceId{ spaceId }
        {}

        size_t GetHash() const;

        using Register = uint32_t;
        static const Register InvalidRegister = ~0u;

        RHI::ShaderStageMask m_shaderStageMask = RHI::ShaderStageMask::None;
        Register             m_registerId      = InvalidRegister;
        uint32_t             m_spaceId         = InvalidRegister;
    };

    struct ShaderResourceBindingInfo
    {
        ShaderResourceBindingInfo() = default;

        size_t GetHash() const;

        ResourceBindingInfo m_constantDataBindingInfo;
        eastl::unordered_map<RHI::InputName, ResourceBindingInfo> m_resourcesRegisterMap;
    };

    //=========================================================================
    // 新路径（ShaderInput）structures
    //=========================================================================

    //! 引用 PipelineLayoutDescriptor 各类型 descriptor 数组里的一条记录。
    //! m_index 是对应类型数组（m_bufferDescs / m_imageDescs / ...）里的下标。
    struct ShaderInputHandle
    {
        ShaderInputType m_type  = ShaderInputType::Count;
        uint32_t        m_index = 0;
    };

    //! 同一 HLSL space 内所有 ShaderInput descriptor 的集合。
    //! 对应 DX12 的一个（或两个，view/sampler heap 分离时）root parameter，
    //! 对应 Vulkan 的一个 VkDescriptorSet（set = m_spaceId）。
    struct ShaderInputGroup
    {
        uint32_t        m_spaceId   = 0;
        ShaderStageMask m_stageMask = ShaderStageMask::None;
        eastl::fixed_vector<ShaderInputHandle, 8> m_shaderInputs;
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
        // 旧路径 API（SRG）— 保持不动
        //---------------------------------------------------------------------

        void AddShaderResourceLayoutInfo(
            const ShaderResourceLayout& layout,
            const ShaderResourceBindingInfo& shaderResourceInfo);

        void SetRootConstantsLayout(const ConstantsLayout& rootConstantsLayout);

        size_t                           GetShaderResourceLayoutCount() const;
        const ShaderResourceLayout*      GetShaderResourceLayout(size_t index) const;
        const ShaderResourceBindingInfo& GetShaderResourceBindingInfo(size_t index) const;
        const ConstantsLayout*           GetRootConstantsLayout() const;
        uint32_t                         GetShaderResourceIndexFromBindingSlot(uint32_t bindingSlot) const;

        //---------------------------------------------------------------------
        // 新路径 API（ShaderInput）
        //---------------------------------------------------------------------

        //! 批量添加一组 descriptor，stageMask 指明使用这组 input 的着色器阶段。
        //! 内部按 spaceId 自动分组到 m_spaceGroups，并对同 space 同类型的
        //! register 重叠做 Validation 检查。
        void AddShaderInputDescriptors(const ShaderInputList& list, ShaderStageMask stageMask);

        //! 添加一个 static sampler，不进入 SpaceGroup（DX12 static sampler 不占 root parameter）。
        void AddStaticSamplerDescriptor(
            const ShaderInputStaticSamplerDescriptor& desc,
            ShaderStageMask stageMask);

        //! 当前描述符是否走新路径（ShaderInput）。
        //! DX12::PipelineLayout::Init 用此 flag 选择构建分支。
        bool UsesShaderInputPath() const;

        // SpaceGroup 访问 — DX12 build loop 和 Compiler 使用
        size_t                      GetSpaceGroupCount() const;
        const ShaderInputGroup& GetSpaceGroup(size_t index) const;

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

        using ShaderResourceLayoutInfo =
            eastl::pair<Ptr<ShaderResourceLayout>, ShaderResourceBindingInfo>;

        const eastl::fixed_vector<
            ShaderResourceLayoutInfo,
            RHI::Limits::Pipeline::ShaderResourceCountMax>&
        GetShaderResourceLayoutInfo() const;

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

        static constexpr size_t InvalidHash    = static_cast<size_t>(~0);

        //---------------------------------------------------------------------
        // 旧路径数据（SRG）
        //---------------------------------------------------------------------
        eastl::fixed_vector<
            ShaderResourceLayoutInfo,
            RHI::Limits::Pipeline::ShaderResourceCountMax> m_shaderResourceLayoutsInfo;

        Ptr<ConstantsLayout> m_rootConstantsLayout;

        eastl::array<uint32_t,
            RHI::Limits::Pipeline::ShaderResourceCountMax> m_bindingSlotToIndex = {};

        //---------------------------------------------------------------------
        // 新路径数据（ShaderInput）
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
