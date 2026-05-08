#pragma once

#include <EASTL/array.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/HardwareQueue.h>

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <RenderGraph/RenderGraphCompiler.h>


namespace Spark::RHI
{
    class CommandList;
    class Device;
    class Factory;
}


namespace Spark::Render
{
    //! draw call 子区间,左闭右开 [m_startIndex, m_endIndex)。
    //! m_endIndex == 0 表示整个 pass (等同于 startIndex=0 且不截断)。
    struct DrawRange
    {
        uint32_t m_startIndex = 0;
        uint32_t m_endIndex   = 0;
    };

    //! 一个 CommandList 的录制内容:若干 (pass, 区间) 按序录制。
    struct ExecuteWork
    {
        struct Item
        {
            Pass      m_pass;
            DrawRange m_draws;
            uint32_t  m_itemIndex = 0;
            uint32_t  m_itemCount = 1;
        };
        eastl::vector<Item>    m_items;
        RHI::CommandList*      m_commandList = nullptr;
    };

    //! 一组 Work 并行录制,GPU 按数组顺序执行,对应一次 ExecuteCommandLists。
    //! m_passes 由 BuildExecuteGroups 填充,m_works 由 BuildExecuteWorks 填充。
    struct ExecuteGroup
    {
        eastl::vector<Pass>        m_passes;
        eastl::vector<ExecuteWork> m_works;
    };

    //! 一次跨队列同步区间:waits 之后、signal 之前,包含若干次 GPU submission
    //! (每个 ExecuteGroup 一次 ExecuteCommandLists)。
    //! m_passes 由 BuildSegments 填充,m_groups 由 BuildExecuteGroups 填充。
    struct QueueSegment
    {
        eastl::vector<SyncOperation>     m_waits;
        eastl::vector<Pass>              m_passes;
        eastl::vector<ExecuteGroup>      m_groups;
        eastl::optional<SyncOperation>   m_signal;
    };

    using QueueSegments = eastl::array<eastl::vector<QueueSegment>, RHI::HardwareQueueClassCount>;


    class RenderGraphExecuter
    {
    private:
        friend class RenderGraph;

        void BuildExecuteTable(const QueueBasedPasses& queueBasedPasses, const PassContext& passContext);

        QueueSegments& GetQueueSegments() { return m_queueSegments; }

        void BuildSegments(uint32_t queueIndex, eastl::span<const Pass> queuePasses, const PassContext& passContext);

        void BuildExecuteGroups(QueueSegment& segment, const PassContext& passContext) const;

        void BuildExecuteWorks(ExecuteGroup& group, const PassContext& passContext) const;

        void ExecutePreBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecutePostBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteBeginRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        void ExecuteEndRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext);

        //! 创建 CommandList 并录制 ExecuteWork,完成后存入 work.m_commandList。
        void Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext);

        void ExecuteFinalBarriers(RHI::CommandList* commandList);

        QueueSegments m_queueSegments;
    };
}