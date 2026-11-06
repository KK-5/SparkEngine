#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <Hierarchy/HierarchyManager.h>
#include <Material/MaterialSystem.h>
#include <Spawn/ModelSpawnResolver.h>
#include <Transform/TransformSystem.h>
#include "../Common/RenderGraphUtil.h"
#include "ReferencePathTracerFeature.h"

int main(int argc, char** argv)
{
    unsigned long mode = 1;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--mode") != 0 || i + 1 >= argc)
        {
            return 1;
        }
        char* end = nullptr;
        errno = 0;
        const char* valueText = argv[++i];
        const unsigned long value = std::strtoul(valueText, &end, 10);
        if (errno != 0 || valueText[0] == '-' || end == valueText || *end != '\0'
            || value < 1 || value > 6)
        {
            return 1;
        }
        mode = value;
    }

    using namespace Spark;
    auto sys = SandBox::InitRenderGraphApp(960, 960, "ReferencePathTracer");
    auto hierarchy = CreateSystem<HierarchyManager>();
    hierarchy->Init();
    auto transforms = CreateSystem<Transform::TransformSystem>();
    transforms->Init();
    auto materials = CreateSystem<Material::MaterialSystem>();
    materials->Init();

    int result = 0;
    {
        Spawn::ModelSpawnResolver spawnResolver;
        spawnResolver.Init();

        Render::Pipeline pipeline("ReferencePathTracer");
        Render::PassExecuteContext::Push(pipeline.GetPassContext());
        sys.m_render->SetUpSystemPasses(pipeline.GetPassContext());
        SandBox::ReferencePathTracerFeature feature;
        if (feature.Init(static_cast<uint32_t>(mode - 1)))
        {
            while (!sys.m_window->ShouldClose())
            {
                TickBus::Broadcast(&TickBus::Events::OnTick, FrameTime{});
            }
        }
        else
        {
            result = 1;
        }
        feature.Shutdown();
        Render::PassExecuteContext::Pop();
    }

    // Keep the material context alive while renderer shutdown retires MaterialSlot
    // allocations. Texture owners must then go before the RHI context and its pools.
    sys.m_asyncUpload.reset();
    sys.m_render.reset();
    materials.reset();
    transforms.reset();
    hierarchy.reset();
    return result;
}
