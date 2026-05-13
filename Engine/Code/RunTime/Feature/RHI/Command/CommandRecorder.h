#pragma once

#include <RHI/HardwareQueue.h>
#include <RHI/Device/DeviceObject.h>

namespace Spark::RHI
{
    class CommandList;

    struct CommandRecorderDescriptor
    {
        RHI::HardwareQueueClass m_queue = RHI::HardwareQueueClass::Graphics;
    };

    // CommandRecorder是CommandList的轻量级独立封装，绑定专属的allocator，
    // 提供显式的Reset()控制录制周期。适用于帧循环外的异步录制场景
    // （如AsyncUploadSystem），每个recorder可以独立完成 reset -> record -> submit 周期，
    // 不与CommandListAllocator池竞争，生命周期通过Ptr管理。
    class CommandRecorder : public DeviceObject
    {
    public:
        virtual ~CommandRecorder() = default;

        RHI::ResultCode Init(Device& device, const CommandRecorderDescriptor& desc);

        void Shutdown();

        void Reset();

        CommandList* GetCommandList();
        const CommandList* GetCommandList() const;

        const CommandRecorderDescriptor& GetDesc() const;

    protected:
        void SetCommandList(CommandList* cl) { m_commandList = cl; }

    private:
        ////////////////////////////////////////////
        // Backend api
        virtual RHI::ResultCode InitInternal(Device& device, const CommandRecorderDescriptor& desc) = 0;
        virtual void ShutdownInternal() = 0;
        virtual void ResetInternal() = 0;
        ////////////////////////////////////////////


        CommandList*              m_commandList = nullptr;
        CommandRecorderDescriptor m_desc;
    };
}