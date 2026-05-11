#pragma once

#include <Base.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <Pass/RHIHandle.h>

namespace Spark::RHI
{
    class Device;
    class BufferPool;
    class Buffer;
    class ShaderResourceLayout;
    class ShaderResource;
}

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::SandBox
{
    class TrianglePassFeature : public TickBus::Handler
    {
    public:
        TrianglePassFeature();
        ~TrianglePassFeature();

        bool Init();
        void Shutdown();

        // TickBus
        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(Spark::RenderSystemTickOrder) - 1;
        }

    private:
        void CreateViewSRG();
        void CreateTrianglePass();
        void CreateVertexBuffer();
        void UpdateViewSRG();

        // ViewSRG (in RHIContext)
        Spark::Render::RHIHandle  m_viewSRGEntity = Spark::Render::NullHandle;
        Spark::RHI::ShaderResource* m_srg = nullptr;

        // Resources
        Ptr<Spark::RHI::BufferPool> m_bufferPool;
        Ptr<Spark::RHI::Buffer>     m_vertexBuffer;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_vertShader;
        Ptr<Spark::Resource::ShaderAsset> m_fragShader;

        // Transform
        float m_rotationAngle = 0.f;
    };
}
