#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include <ECS/ISystem.h>
#include <Service/Service.h>
#include <Tick/TickBus.h>

namespace Editor
{
    //! What the menus ask of the scene. They cannot do it where they are asked: a menu callback
    //! runs inside the UI pass, which is the render graph recording a command list.
    class ISceneCommands
    {
    public:
        virtual ~ISceneCommands() = default;

        virtual void Save(eastl::string_view virtualPath) = 0;
        virtual void Open(eastl::string_view virtualPath) = 0;
        virtual void New() = 0;
    };

    //! Runs what was asked, at the one point in the frame where the world is nobody's to hold:
    //! after the render graph, and after EntityReaper has taken the frame's deletions -- so a
    //! save sees the scene the user is looking at, links included.
    class SceneCommandSystem final : public Spark::ISystem,
                                     public Spark::TickBus::Handler,
                                     public Spark::Service<ISceneCommands>::Handler
    {
    public:
        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;
        eastl::vector<Spark::HashString> Request() const override { return {}; }
        Spark::HashString GetName() const override { return "SceneCommandSystem"; }

        // TickBus
        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override;

        // ISceneCommands
        void Save(eastl::string_view virtualPath) override;
        void Open(eastl::string_view virtualPath) override;
        void New() override;

    private:
        eastl::string m_save;
        eastl::string m_open;
        bool          m_new{false};
    };
}
