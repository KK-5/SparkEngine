#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Context/RHIHandle.h>

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <RenderGraph/RenderGraphCompiler.h>


namespace Spark::RHI
{
    class CommandList;
    class CommandQueueContext;
    class Device;
    class Factory;
    struct RenderPassBeginInfo;
}


namespace Spark::Render
{
    //! What an opaque Scope's hook is handed: the CommandList it records into, and one view
    //! segment of the Scope's submit range at a time.
    struct ExecuteWork
    {
        RHI::CommandList* m_commandList = nullptr;

        //! The segment's items. m_submitBase is the submit index its first entry must be
        //! submitted with (what CommandList::ValidateSubmitIndex checks against SetSubmitRange).
        eastl::span<const RHI::RHIHandle> m_itemHandles;
        uint32_t                          m_submitBase = 0;
    };

    class RenderGraphExecuter;
    struct ScopeState;

    class RenderGraphExecuter
    {
    public:
        uint32_t GetFrameIndex() const { return m_frameIndex; }

    private:
        friend class RenderGraph;

        void Begin(uint32_t frameIndex);

        void End();

        void ExecuteStaticPreBarriers(RHI::CommandList* commandList, uint32_t queueIndex);

        void SetStaticPreBarriers(StaticPreBarrierTable&& table) { m_staticPreBarriers = eastl::move(table); }

        //! Records and submits every Scope in stream order. Each of activeQueues is opened first
        //! with its static-import waits and barriers, then keeps one CommandList open, submitted
        //! before a Scope that waits or carries WorkStartTag, and after one that signals.
        void ExecuteScopes(
            RHIContext& rhiContext, RHI::HardwareQueueClassMask activeQueues,
            RHI::Factory& factory, RHI::Device& device, RHI::CommandQueueContext& queues);

        //! A Scope's own work, between its barriers: its render pass if it has one, its state,
        //! and its submit range.
        void RecordScope(RHI::CommandList* commandList, RHIContext& rhiContext, RHIHandle scope);

        //! Walk the Scope's submit range: each view handle sets viewport and space1 for the items
        //! after it, which go to the Scope's hook if it has one, else are submitted one by one.
        void SubmitScopeRange(
            RHI::CommandList* commandList, RHIContext& rhiContext, RHIHandle scope,
            const ScopeState& state, const RHI::RenderPassBeginInfo* beginInfo);

        eastl::vector<RHI::RHIHandle>& GetSubmitList() { return m_submitList; }

        StaticPreBarrierTable m_staticPreBarriers;

        //! This frame's submit sequence, written by lowering: each Scope's ScopeSubmitRange is a
        //! stretch of it. A handle is either a view, whose viewport and space1 the items after
        //! it are submitted under, or an item.
        eastl::vector<RHI::RHIHandle> m_submitList;

        uint32_t m_frameIndex { 0 };
    };
}
