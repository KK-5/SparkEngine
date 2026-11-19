#include <gtest/gtest.h>

#include "FakeDevice.h"

#include <Pass/Component/ScopeComponents.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    class TestLayout final : public RHI::PipelineLayoutDescriptor
    {
    };

    class TrackedBindings final : public RHI::ShaderBindings
    {
    public:
        uint32_t m_shutdowns = 0;

    private:
        RHI::ResultCode InitInternal(RHI::Device&, const Descriptor&) override
        {
            return RHI::ResultCode::Success;
        }

        void ShutdownInternal() override { ++m_shutdowns; }
    };
}

TEST(ScopeBindingTest, DestroyingAScopeReleasesOnlyItsOwnGroups)
{
    TrackedBindings localConstants;
    TrackedBindings localResources;
    TrackedBindings anotherScope;
    RHI::RHIContext context;
    const auto first = context.CreateEntity();
    const auto second = context.CreateEntity();
    context.Add<Scope>(first);
    context.Add<Scope>(second);

    auto& firstBindings = context.Add<ScopeBinding>(first);
    firstBindings.m_groups.emplace_back(&localConstants);
    firstBindings.m_groups.emplace_back(&localResources);
    context.Add<ScopeBinding>(second).m_groups.emplace_back(&anotherScope);

    context.DestoryEntity(first);
    EXPECT_EQ(localConstants.m_shutdowns, 1u);
    EXPECT_EQ(localResources.m_shutdowns, 1u);
    EXPECT_EQ(anotherScope.m_shutdowns, 0u);

    context.DestoryEntity(second);
    EXPECT_EQ(anotherScope.m_shutdowns, 1u);
}

TEST(ScopeBindingTest, ARecycledBindingStartsCleanOfValuesAndNeedsCompilation)
{
    constexpr uint32_t spaceId = 0;
    const RHI::InputName input("g_ScopeValue");
    RHI::ShaderInputList inputs;
    inputs.m_constants.emplace_back(input, 0, sizeof(uint32_t), 0, spaceId);
    Ptr<TestLayout> layout(new TestLayout);
    layout->AddShaderInputDescriptors(inputs);
    ASSERT_EQ(layout->Finalize(), RHI::ResultCode::Success);

    Spark::Test::FakeDevice device;
    TrackedBindings bindings;
    const RHI::ShaderBindings::Descriptor desc{ layout, spaceId };
    ASSERT_EQ(bindings.Init(device, desc), RHI::ResultCode::Success);
    auto* constant = bindings.FindConstantInput(input);
    ASSERT_NE(constant, nullptr);
    const uint32_t firstValue = 1;
    ASSERT_TRUE(constant->SetData(&firstValue, sizeof(firstValue)));
    bindings.ClearDirty();
    bindings.Shutdown();

    ASSERT_EQ(bindings.Init(device, desc), RHI::ResultCode::Success);
    EXPECT_TRUE(bindings.IsDirty());
    constant = bindings.FindConstantInput(input);
    ASSERT_NE(constant, nullptr);
    const uint32_t zero = 0;
    EXPECT_TRUE(constant->IsSameData(&zero, sizeof(zero)));
    bindings.Shutdown();
}
