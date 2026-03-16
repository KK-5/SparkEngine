#include <EASTL/unique_ptr.h>

#include <Log/SpdLogSystem.h>

#include <RHI/RHIInterface.h>

#include <RHI/Backend/DX12/RHISystem.h>


namespace Spark::SandBox
{
    class HelloTriangle
    {
    public:
        HelloTriangle();
        ~HelloTriangle();

        void Init();

    private:
        void CreateDevice();
        void CreateCommandQueue();
        void CreateFence();

        Ptr<RHI::Device> m_device;
        Ptr<RHI::CommandQueue> m_commandQueue;
        Ptr<RHI::Fence> m_fence;

        RHI::Factory* m_rhiFactory;

        eastl::unique_ptr<ILogSystem<SpdLogSystem>> m_logger;
        eastl::unique_ptr<Spark::RHI::RHIInterface> m_rhi;

    };


    HelloTriangle::HelloTriangle()
    {
        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logger = eastl::make_unique<SpdLogSystem>(logConfig);

        m_rhi = eastl::make_unique<Spark::RHI::DX12::RHISystem>();
        m_rhi->Initialize();

        m_rhiFactory = Service<Spark::RHI::RHIInterface>::Get()->GetRHIFactory();
        if (!m_rhiFactory)
        {
            LOG_ERROR("Get RHI Factory failed");
        }
    }

    HelloTriangle::~HelloTriangle()
    {
        m_rhi->FactoryCollect();
        m_rhi->Shutdown();
    }

    void HelloTriangle::CreateDevice()
    {
        RHI::PhysicalDeviceList devList = m_rhiFactory->EnumeratePhysicalDevices();
        for (Ptr<RHI::PhysicalDevice> physicalDev: devList)
        {
            LOG_INFO(physicalDev->GetDescriptor().m_description.c_str());
        }

        m_device = m_rhiFactory->CreateDevice();
        RHI::DeviceDescriptor desc;
        desc.m_frameCountMax = 1;
        RHI::ResultCode result = m_device->Init(*devList[0], desc);
        if (result == RHI::ResultCode::Success)
        {
            LOG_INFO("Create Device successed!");
        }
    }

    void HelloTriangle::CreateCommandQueue()
    {
        m_commandQueue = m_rhiFactory->CreateCommandQueue();
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        RHI::ResultCode result = m_commandQueue->Init(*m_device, desc);
        if (result == RHI::ResultCode::Success)
        {
            LOG_INFO("Create command queue successed!");
        }
    }

    void HelloTriangle::CreateFence()
    {
        m_fence = m_rhiFactory->CreateFence();
        RHI::ResultCode result = m_fence->Init(*m_device, RHI::FenceState::Reset);
        if (result == RHI::ResultCode::Success)
        {
            LOG_INFO("Create fence successed!");
        }
    }

    void HelloTriangle::Init()
    {
        CreateDevice();
        CreateCommandQueue();
        CreateFence();
    }


}


int main(int argc, char **argv)
{
    using namespace Spark;

    Spark::SandBox::HelloTriangle app;

    app.Init();

    return 0;
}
