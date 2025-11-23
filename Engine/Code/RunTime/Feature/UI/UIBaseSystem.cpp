#include "UIBaseSystem.h"


namespace Spark::UI
{
    UIBaseSystem::TickHandlerFrameEnd::TickHandlerFrameEnd(UIBaseSystem& UISystem): m_UISystem(UISystem)
    {
        TickBus::Handler::BusConnect();
    }

    UIBaseSystem::TickHandlerFrameEnd::~TickHandlerFrameEnd()
    {
        TickBus::Handler::BusDisconnect();
    }

    UIBaseSystem::TickHandlerFrameStart::TickHandlerFrameStart(UIBaseSystem& UISystem): m_UISystem(UISystem)
    {
        TickBus::Handler::BusConnect();
    }

    UIBaseSystem::TickHandlerFrameStart::~TickHandlerFrameStart()
    {
        TickBus::Handler::BusDisconnect();
    }

    UIBaseSystem::TickHandlerAfterInput::TickHandlerAfterInput(UIBaseSystem& UISystem): m_UISystem(UISystem)
    {
        TickBus::Handler::BusConnect();
    }

    UIBaseSystem::TickHandlerAfterInput::~TickHandlerAfterInput()
    {
        TickBus::Handler::BusDisconnect();
    }

    void UIBaseSystem::Initialize()
    {
        m_tickHandlerFrameStart = eastl::make_unique<TickHandlerFrameStart>(*this);
        m_tickHandlerAfterInput = eastl::make_unique<TickHandlerAfterInput>(*this);
        m_tickHandlerFrameEnd = eastl::make_unique<TickHandlerFrameEnd>(*this);
    }

    void UIBaseSystem::TickHandlerFrameStart::OnTick([[maybe_unused]]WorldContext& context, [[maybe_unused]]float deltaTime)
    {
        m_UISystem.NewFrame();
    }

    void UIBaseSystem::TickHandlerAfterInput::OnTick(WorldContext& context, [[maybe_unused]]float deltaTime)
    {
        m_UISystem.DrawUI(context);
    }

    void UIBaseSystem::TickHandlerFrameEnd::OnTick([[maybe_unused]]WorldContext& context, [[maybe_unused]]float deltaTime)
    {
        m_UISystem.EndFrame();
    }
}