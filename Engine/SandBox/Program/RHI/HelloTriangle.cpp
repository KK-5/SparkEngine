#include <EASTL/unique_ptr.h>

#include <Log/SpdLogSystem.h>

#include <RHI/RHIInterface.h>

#include <RHI/Backend/DX12/RHISystem.h>


namespace Spark::SandBox
{
    class HelloTriangleAPP
    {
    public:
        HelloTriangleAPP();
        ~HelloTriangleAPP();

        void BuildRHIResource();

    private:
        eastl::unique_ptr<ILogSystem<SpdLogSystem>> m_logger;
        eastl::unique_ptr<Spark::RHI::RHIInterface> m_rhi;

    };


    HelloTriangleAPP::HelloTriangleAPP()
    {
        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logger = eastl::make_unique<SpdLogSystem>(logConfig);

        m_rhi = eastl::make_unique<Spark::RHI::DX12::RHISystem>();
        m_rhi->Initialize();
    }

    HelloTriangleAPP::~HelloTriangleAPP()
    {
        
    }

    void HelloTriangleAPP::BuildRHIResource()
    {
        RHI::Factory* rhiFac = Service<Spark::RHI::RHIInterface>::Get()->GetRHIFactory();

        if (!rhiFac)
        {
            LOG_ERROR("Get RHI Factory failed");
        }

        RHI::PhysicalDeviceList devList = rhiFac->EnumeratePhysicalDevices();
        for (Ptr<RHI::PhysicalDevice> physicalDev: devList)
        {
            LOG_INFO(physicalDev->GetDescriptor().m_description.c_str());
        }

        Ptr<RHI::Device> device = rhiFac->CreateDevice();
        device->Init(*devList[0]);
    }


}


int main(int argc, char **argv)
{
    using namespace Spark;

    Spark::SandBox::HelloTriangleAPP app;

    app.BuildRHIResource();

    return 0;
}
