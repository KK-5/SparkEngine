#pragma once

#include <EASTL/unique_ptr.h>
#include <EASTL/any.h>

#include <ECS/ISystem.h>
#include <ECS/WorldContext.h>
#include <HashString/HashString.h>
#include <Tick/TickBus.h>

namespace Spark::UI
{
    class UIBaseSystem : public ISystem
    {
    public:
        virtual ~UIBaseSystem() = default;
        // ISystem
        void Initialize() override;

        eastl::vector<HashString> Request() const override
        {
            return {"RenderSystem"_hs};
        }

        HashString GetName() const override
        {
            return "UISystem"_hs;
        }

        ///////////////////////////
        virtual void NewFrame()                    = 0;    // 每帧开始时被调用，可以设置ui环境
        virtual void DrawUI(WorldContext& context) = 0;    // 输入系统结束之后，场景渲染之前被调用
        virtual void EndFrame()                    = 0;    // 所有渲染之后被调用

        virtual eastl::any GetUIRenderData() = 0;  // 获取UI的渲染数据，交给渲染系统渲染

        virtual bool WantCaptureMouse()    const = 0;  // 检查UI是否需要捕获鼠标事件，如果是，此事件不应被输入系统响应
        virtual bool WantCaptureKeyboard() const = 0;  // 同上，检查UI是否需要捕获键盘事件

    protected:
        class TickHandlerFrameStart final : public TickBus::Handler
        {
        public:
            TickHandlerFrameStart(UIBaseSystem& UISystem);
            ~TickHandlerFrameStart();

            void OnTick(WorldContext& context, float deltaTime) override;

            inline unsigned int GetTickOrder() const override
            {
                return static_cast<unsigned int>(TickOrder::TICK_FIRST);
            }
        private:
            UIBaseSystem& m_UISystem;
        };

        class TickHandlerAfterInput final : public TickBus::Handler
        {
        public:
            TickHandlerAfterInput(UIBaseSystem& UISystem);
            ~TickHandlerAfterInput();

            void OnTick(WorldContext& context, float deltaTime) override;

            inline unsigned int GetTickOrder() const override
            {
                return static_cast<unsigned int>(TickOrder::TICK_INPUT + 1);
            }
        private:
            UIBaseSystem& m_UISystem;
        };

        class TickHandlerFrameEnd final : public TickBus::Handler
        {
        public:
            TickHandlerFrameEnd(UIBaseSystem& UISystem);
            ~TickHandlerFrameEnd();

            void OnTick(WorldContext& context, float deltaTime) override;

            inline unsigned int GetTickOrder() const override
            {
                return static_cast<unsigned int>(TickOrder::TICK_UI);
            }
        private:
            UIBaseSystem& m_UISystem;
        };

        eastl::unique_ptr<TickHandlerFrameStart> m_tickHandlerFrameStart;
        eastl::unique_ptr<TickHandlerAfterInput> m_tickHandlerAfterInput;
        eastl::unique_ptr<TickHandlerFrameEnd>   m_tickHandlerFrameEnd;
    };
}