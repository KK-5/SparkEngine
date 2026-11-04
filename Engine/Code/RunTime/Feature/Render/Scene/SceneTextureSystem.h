#pragma once

#include <ECS/WorldContext.h>
#include "SceneTextureComponents.h"

namespace Spark::RHI
{
    class Device;
    class ImagePool;
}

namespace Spark::Render
{
    //! Renderer-owned static scene images: one device pool, one upload per image, no frame copies.
    class SceneTextureSystem
    {
    public:
        SceneTextureSystem();
        ~SceneTextureSystem();
        void Init(RHI::RHIContext& rhiCtx, RHI::Device& device);
        void Update(WorldContext& world, RHI::RHIContext& rhiCtx);
        //! Retires owned handles. The renderer's common reaper runs before this object's pool goes.
        void Shutdown(WorldContext& world);

    private:
        //! Creates immediately and submits one asynchronous upload. The request retains its source.
        RHI::RHIHandle CreateImage(RHI::RHIContext& rhiCtx, const Ptr<Resource::ImageAsset>& asset, ObjectName name);

        template<typename Tag>
        RHI::UniqueRHIHandle CreateLut(RHI::RHIContext& rhiCtx, const char* path, const char* name);
        void CreateSkybox(WorldContext& world, RHI::RHIContext& rhiCtx, Entity entity,
            const Ptr<Resource::ImageAsset>& asset);

        Ptr<RHI::ImagePool> m_pool;
        RHI::UniqueRHIHandle m_brdfLut;
        RHI::UniqueRHIHandle m_ltc1;
        RHI::UniqueRHIHandle m_ltc2;
    };
}
