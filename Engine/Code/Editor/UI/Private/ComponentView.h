#pragma once


namespace Spark
{
    class WorldContext;
}

namespace Editor
{
    class ComponentView final
    {
    public:
        void Draw(Spark::WorldContext& context);

    private:
        void DrawComponent();
    };
}