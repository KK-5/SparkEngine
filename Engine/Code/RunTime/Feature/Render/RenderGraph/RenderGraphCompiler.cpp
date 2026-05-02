#include "RenderGraphCompiler.h"

#include <Log/SpdLogSystem.h>

#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
    QueueBasedPasses RenderGraphCompiler::CompilePassCrossQueue(eastl::span<Pass> passes)
    {
        auto& passContext = *PassExecuteContext::Current();

        QueueBasedPasses result;

        // 每个队列的时间线
        // 记录每个Pass在自己队列上的时间线位置1
        eastl::unordered_map<Pass, uint32_t> timelinePos;
        timelinePos.reserve(passes.size());

        for(auto pass : passes)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());
            const auto queue = passContext.Get<PassExecuteQueue>(pass).m_queue;
            const auto queueIndex = static_cast<size_t>(queue);

            timelinePos[pass] = static_cast<uint32_t>(result[queueIndex].size());
            result[queueIndex].push_back(pass);

            if (!passContext.Has<PassPredecessors>(pass))
            {
                continue;
            }

            // 跨队列压缩: 每个源队列只留 timelinePos 最大的 pred
            eastl::array<Pass,     RHI::HardwareQueueClassCount> latestPred{ NullPass, NullPass, NullPass };
            eastl::array<uint32_t, RHI::HardwareQueueClassCount> latestPos { 0, 0, 0 };

            for (Pass pred: passContext.Get<PassPredecessors>(pass).m_preds)
            {
                ASSERT(passContext.Has<PassExecuteQueue>(pred), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pred).m_name.GetCStr());
                const auto predQueue = passContext.Get<PassExecuteQueue>(pred).m_queue;
                const auto predQueueIndex = static_cast<size_t>(predQueue);
                ASSERT(timelinePos.contains(pred), "Invalid pred pass {}", passContext.Get<PassName>(pred).m_name.GetCStr());
                uint32_t predPos = timelinePos[pred];
                // 记录最晚的 pred pass
                if (latestPred[predQueueIndex] == NullPass || predPos > latestPos[predQueueIndex])
                {
                    latestPred[predQueueIndex] = pred;
                    latestPos[predQueueIndex] = predPos;
                }
            }

            for (uint32_t queue = 0; queue < RHI::HardwareQueueClassCount; ++queue)
            {
                // 此队列无前驱依赖
                if (latestPred[queue] == NullPass) 
                {
                    continue;
                }

                const auto queueSrc = static_cast<RHI::HardwareQueueClass>(queue);
                const uint64_t value = latestPos[queue];

                if (passContext.Has<PassSyncWait>(pass))
                {
                    passContext.Get<PassSyncWait>(pass).m_waits.emplace_back(SyncOperation{ queueSrc, value });
                }
                else
                {
                    PassSyncWait wait;
                    wait.m_waits.emplace_back(queueSrc, value);
                    passContext.Add<PassSyncWait>(pass, wait);
                }

                // pred 自己队列上只 signal 一次
                if (!passContext.Has<PassSyncSignal>(latestPred[queue]))
                {
                    passContext.Add<PassSyncSignal>(latestPred[queue], SyncOperation{ queueSrc, value });
                }
            }
        }

        return result;
    }
    
}