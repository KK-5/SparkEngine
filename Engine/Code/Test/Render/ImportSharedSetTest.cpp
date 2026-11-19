#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <RenderGraph/PassScopes.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <Binding/ShaderUsage.h>
#include <Material/MaterialTextureComponents.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    using Members = eastl::vector<RHI::RHIHandle>;

    //! An Image with no backend: the set only asks whether one is there.
    class SetImage final : public RHI::Image
    {
    };

    class TestSharedLayout final : public RHI::PipelineLayoutDescriptor
    {
    };

    //! A material texture as the renderer makes one: a ready Image publishing the bindless
    //! usage that the material tables' indices are resolved through.
    RHI::RHIHandle AddMaterialTexture(RHI::RHIContext& context)
    {
        const RHI::RHIHandle entity = context.CreateEntity();
        context.Add<RHI::Components::Image>(entity, RHI::Components::Image{ Ptr<RHI::Image>(new SetImage) });
        context.Add<RHI::ResourceName>(entity, RHI::ResourceName{ ObjectName("MaterialTex") });
        context.Add<MaterialTextureTag>(entity);
        context.Add<ShaderImageUsage>(entity, ShaderImageUsage{
            ShaderInputBinding::Bindless(), RHI::ImageViewDescriptor{} });
        return entity;
    }
}

//! One frame of one pass, driven the way the graph drives one: the builder opened through its
//! scopes, then read back for what the pass declared.
class ImportSharedSetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_pass = m_passContext.CreatePass();
        m_passContext.Add<PassName>(m_pass, PassName{ ObjectName("SetTestPass") });
        m_passContext.Add<PassExecuteQueue>(m_pass, PassExecuteQueue{ RHI::HardwareQueueClass::Graphics });

        PassExecuteContext::Push(m_passContext);
        RHI::RHIExecuteContext::Push(m_rhi);
        m_builder.Begin(0, RHI::NullHandle, Math::Vector2Int(16, 16), Math::Vector2Int(16, 16));
        m_builder.BeginPass(m_pass);
    }

    void TearDown() override
    {
        RHI::RHIExecuteContext::Pop();
        PassExecuteContext::Pop();
    }

    //! Import `members` as the set `name`. The import entry the scopes call is friend-only, so
    //! every test comes through here rather than reaching for the builder itself.
    void ImportSet(const char* name, Members members)
    {
        m_builder.ImportSharedResourceSet(RHI::AttachmentId(name), eastl::move(members));
    }

    void ImportShared(const RHI::AttachmentId& name, RHI::RHIHandle resource)
    {
        m_builder.ImportSharedResource(name, resource);
    }

    //! Import `members` as `name` and read the set in a Scope of its own.
    RenderScope ImportAndRead(RenderPassScopes& scopes, const char* name, Members members)
    {
        ImportSet(name, eastl::move(members));
        RenderScope scope = scopes.Scope();
        scope.ReadSet(RHI::AttachmentId(name)).Stage(RHI::AttachmentStage::FragmentShader);
        return scope;
    }

    //! What the Scope opened last declared: one entry per member its set read expanded into,
    //! each under the resource's own name rather than the set's.
    Members ScopeAccesses() const
    {
        return Members(m_builder.m_scopeAttachments.begin(), m_builder.m_scopeAttachments.end());
    }

    RenderGraphBuilder m_builder;
    RHI::RHIContext    m_rhi;
    PassContext        m_passContext;
    Pass               m_pass = NullPass;
};

//! Nothing carries the tag: importing and reading the set leaves the Scope with nothing to
//! declare, which is not a reason to skip the pass. Closing then waits for the pass's other
//! attachments, so the Scope is left open here.
TEST_F(ImportSharedSetTest, EmptySetDeclaresNoAccess)
{
    RenderPassScopes scopes(m_builder);
    ImportSet("Textures", {});
    RenderScope scope = scopes.Scope();
    scope.ReadSet(RHI::AttachmentId("Textures"));

    EXPECT_TRUE(ScopeAccesses().empty());
}

//! One attachment per member, whatever members the set holds.
TEST_F(ImportSharedSetTest, EachMemberIsOneAccess)
{
    RenderPassScopes scopes(m_builder);
    RenderScope scope = ImportAndRead(scopes, "Textures",
        { AddMaterialTexture(m_rhi), AddMaterialTexture(m_rhi) });

    EXPECT_EQ(ScopeAccesses().size(), 2u);
    scope.Close();
}

//! A member named twice is one member: the textures are a set, not a list of bindings.
TEST_F(ImportSharedSetTest, DuplicateMembersCollapse)
{
    RenderPassScopes scopes(m_builder);
    const RHI::RHIHandle texture = AddMaterialTexture(m_rhi);
    RenderScope scope = ImportAndRead(scopes, "Textures", { texture, texture, texture });

    EXPECT_EQ(ScopeAccesses().size(), 1u);
    scope.Close();
}

//! Members whose upload has not finished are left out: the material table names them as
//! invalid, and the graph never reads a resource whose contents are not there.
TEST_F(ImportSharedSetTest, MembersStillUploadingAreDropped)
{
    RenderPassScopes scopes(m_builder);
    const RHI::RHIHandle ready   = AddMaterialTexture(m_rhi);
    const RHI::RHIHandle pending = AddMaterialTexture(m_rhi);
    m_rhi.Add<RHI::PendingUploads>(pending, RHI::PendingUploads{ 1 });

    RenderScope scope = ImportAndRead(scopes, "Textures", { ready, pending });

    const Members attachments = ScopeAccesses();
    ASSERT_EQ(attachments.size(), 1u);
    EXPECT_EQ(m_rhi.Get<ImagePassAttachment>(attachments[0]).m_image, ready);
    scope.Close();
}

//! A bindless member is read where it publishes: the access carries the resource and its
//! view, and no fixed input is registered for the compiler to fill.
TEST_F(ImportSharedSetTest, BindlessMemberRegistersNoFixedInput)
{
    RenderPassScopes scopes(m_builder);
    const auto resource = AddMaterialTexture(m_rhi);
    RenderScope scope = ImportAndRead(scopes, "Textures", { resource });

    const Members attachments = ScopeAccesses();
    ASSERT_EQ(attachments.size(), 1u);
    EXPECT_FALSE(m_rhi.Has<SharedBindingImportTag>(resource));
    EXPECT_FALSE(m_rhi.Has<ShaderInputBinding>(attachments[0]));
    EXPECT_FALSE(m_rhi.Has<IndexBinding>(attachments[0]));
    EXPECT_EQ(m_rhi.Get<ImagePassAttachment>(attachments[0]).m_access, RHI::AttachmentAccess::Read);
    scope.Close();
}

TEST_F(ImportSharedSetTest, DirectImportMarksResourceAndLeavesOnlyAccessOnEachScope)
{
    constexpr uint32_t space = kSceneSpaceId;
    const RHI::InputName input("g_SharedImage");
    const RHI::AttachmentId name("SharedImage");
    const auto resource = AddMaterialTexture(m_rhi);
    const auto view = RHI::ImageViewDescriptor::Create(RHI::Format::Unknown, 0, 0);
    m_rhi.AddOrReplace<ShaderImageUsage>(resource, ShaderImageUsage{ { space, input, 0 }, view });

    RHI::ShaderInputList inputs;
    auto& descriptor = inputs.m_images.emplace_back(
        input, RHI::ShaderInputImageAccess::Read, RHI::ShaderInputImageType::Image2D, 1, 0, space);
    descriptor.m_stageMask = RHI::ShaderStageMask::Fragment;
    Ptr<RHI::PipelineLayoutDescriptor> layout(new TestSharedLayout);
    layout->AddShaderInputDescriptors(inputs);
    ASSERT_EQ(layout->Finalize(), RHI::ResultCode::Success);
    PassPipelineLayout passLayout;
    passLayout.m_layout = layout;
    passLayout.m_sharedSpaces.push_back(space);
    m_passContext.Add<PassPipelineLayout>(m_pass, eastl::move(passLayout));

    ImportShared(name, resource);
    ImportShared(name, resource);
    EXPECT_TRUE(m_rhi.Has<SharedBindingImportTag>(resource));

    RenderPassScopes scopes(m_builder);
    for (uint32_t i = 0; i < 2; ++i)
    {
        RenderScope scope = scopes.Scope();
        const auto attachment = scope.ReadImage(name).GetHandle();
        const auto& access = m_rhi.Get<ImagePassAttachment>(attachment);
        EXPECT_EQ(access.m_image, resource);
        EXPECT_EQ(access.m_stage, RHI::AttachmentStage::FragmentShader);
        EXPECT_EQ(access.m_viewDescriptor.m_mipSliceMax, view.m_mipSliceMax);
        EXPECT_TRUE(m_rhi.Has<SharedDirectAccessTag>(attachment));
        EXPECT_FALSE(m_rhi.Has<ShaderInputBinding>(attachment));
        scope.Close();
    }
}

//! Two Scopes read the set once each: the set is expanded per Scope, and the second read
//! repeats nothing of the first.
TEST_F(ImportSharedSetTest, EachScopeReadsTheSetOnItsOwn)
{
    RenderPassScopes scopes(m_builder);
    const RHI::RHIHandle texture = AddMaterialTexture(m_rhi);

    RenderScope first = ImportAndRead(scopes, "Textures", { texture });
    EXPECT_EQ(ScopeAccesses().size(), 1u);
    first.Close();

    RenderScope second = scopes.Scope();
    second.ReadSet(RHI::AttachmentId("Textures")).Stage(RHI::AttachmentStage::FragmentShader);
    EXPECT_EQ(ScopeAccesses().size(), 1u);
    second.Close();
}

//! Two passes import the same set: the members are collected in one order whatever order the
//! entities were visited in, so the second import agrees with the first instead of replacing
//! its members.
TEST_F(ImportSharedSetTest, CollectingTwiceAgrees)
{
    const RHI::RHIHandle first  = AddMaterialTexture(m_rhi);
    const RHI::RHIHandle second = AddMaterialTexture(m_rhi);

    ImportSet("Textures", { first, second });
    ImportSet("Textures", { second, first });

    RenderPassScopes scopes(m_builder);
    RenderScope scope = ImportAndRead(scopes, "Textures", { second, first });
    EXPECT_EQ(ScopeAccesses().size(), 2u);
    scope.Close();
}
