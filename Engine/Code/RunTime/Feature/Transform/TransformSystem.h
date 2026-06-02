#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <ECS/Common.h>
#include <SceneManager/Component/HierarchyComponent.h>
#include <Tick/TickBus.h>

#include "Components.h"

namespace Spark::Transform
{
    class TransformSystem final : public ISystem,
                                  public TickBus::Handler
    {
    public:
        SPARK_COMPONENT_ACCESS(
            ReadComponent<HierarchyRootTag>,
            ReadComponent<Hierarchy>,
            ReadComponent<TransformComponent>,
            WriteComponent<LocalTransformMatrix>,
            WriteComponent<WorldTransformMatrix>
        );

        SPARK_SYSTEM_TRAITS(TransformSystem);

        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<HashString> Request() const override
        {
            return {};
        }

        HashString GetName() const override
        {
            return "";
        }

        // TickBus
        void OnTick(float deltaTime) override;

        inline unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(TickOrder::TICK_GAME) - 1;
        }
    };

} // namespace Spark::Transform