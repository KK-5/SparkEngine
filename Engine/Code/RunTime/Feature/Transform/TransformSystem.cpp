#include "TransformSystem.h"

#include <EASTL/algorithm.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Math/MathUtils.h>
#include <Log/ILogSystem.h>
#include <SceneManager/IScene.h>

namespace Spark::Transform
{
    void TransformSystem::InitInternal()
    {
        TickBus::Handler::BusConnect();
    }

    void TransformSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }
    

    void TransformSystem::OnTick(float deltaTime)
    {
        const auto constContext = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto context = WorldExecuteContext::CurrentReference<SystemTraits>();

        auto CalculateLocalMatrix = [](const TransformComponent& transform) ->Math::Matrix4X4
        {
            Math::Matrix4X4 localMatrix = Math::Matrix4X4Const::IDENTITY;
            localMatrix = Math::Translate(localMatrix, transform.m_position);
            localMatrix = Math::Rotate(localMatrix, Math::QuaternionFromEuler(glm::radians(transform.m_rotation)));
            localMatrix = Math::Scale(localMatrix, transform.m_scale);
            return localMatrix;
        };

        auto view = constContext.GetView<TransformComponent>();
        view.each([&](Entity entity, const TransformComponent& transform)
        {
            Math::Matrix4X4 localMatrix = CalculateLocalMatrix(transform);
            context.AddOrReplace<LocalTransformMatrix>(entity, localMatrix);
            if (!constContext.Has<Hierarchy>(entity))
            {
                context.AddOrReplace<WorldTransformMatrix>(entity, localMatrix);
            }
        });

        
        auto* scene = Service<IScene>::Get();
        ASSERT(scene, "IScene is unregister.");

        auto roots = scene->GetRootEntities();
        eastl::vector<Entity> stack;
        for(Entity root: roots)
        {
            stack.push_back(root);

            while(!stack.empty())
            {
                Entity current = stack.back();
                stack.pop_back();

                auto local = context.TryGet<LocalTransformMatrix>(current);
                Math::Matrix4X4 localMatrix = Math::Matrix4X4Const::IDENTITY;
                if (local)
                {
                    localMatrix = local->m_localMatrix;
                }

                auto* h = constContext.TryGet<Hierarchy>(current);
                if(!h)
                {
                    context.AddOrReplace<WorldTransformMatrix>(current, localMatrix);
                    continue;
                }

                Math::Matrix4X4 worldMatrix = localMatrix;
                if (!constContext.Has<HierarchyRootTag>(current))
                {
                    auto parentWorldMatrix = context.TryGet<WorldTransformMatrix>(h->parent);
                    if (parentWorldMatrix)
                    {
                        worldMatrix = parentWorldMatrix->m_worldMatrix * localMatrix;
                    }
                }

                context.AddOrReplace<WorldTransformMatrix>(current, worldMatrix);

                Entity child = h->firstChild;
                while(child != NullEntity)
                {
                    stack.push_back(child);
                    child = constContext.Has<Hierarchy>(child) ? constContext.Get<Hierarchy>(child).nextSibling : NullEntity;
                }
            }
        }
    }
}