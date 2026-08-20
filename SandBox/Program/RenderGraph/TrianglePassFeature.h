#pragma once

#include <Base.h>
#include <Math/Vector3.h>
#include <Math/Matrix4x4.h>
#include <Math/MathUtils.h>
#include <Tick/TickBus.h>

#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>

namespace Spark::RHI
{
    class ShaderBindings;
    class Fence;
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
        void CreateTrianglePass();
        void CreateVertexBuffer();
        void CreateView();
        void Update();
        void BuildGeometry();

        // Vertex buffer (in RHIContext) + raw view entity used to import it as
        // a buffer attachment in the pass — the attachment path is how RG picks
        // up PendingSync from AsyncUploadSystem and emits the queue.Wait + acquire
        // barrier on the graphics queue.
        Spark::RHI::RHIHandle m_vertexBuffer     = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_drawable = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_view = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_shader;

        Spark::Math::Matrix4X4 m_matrix;
        Spark::Math::Vector3   m_colors[3];

        // Transform
        float m_rotationAngle = 0.f;

        float m_colorPhase = 0.f;
    };
}
