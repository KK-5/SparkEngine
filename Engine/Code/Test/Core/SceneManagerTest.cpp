#include <gtest/gtest.h>
#include <EASTL/array.h>

#include <ECS/WorldContext.h>
#include <ECS/Tag.h>
#include <Service/Service.h>
#include <Log/SpdLogSystem.h>

#include <SceneManager/Component/HierarchyComponent.h>
#include <SceneManager/IScene.h>
#include <SceneManager/SceneManager.h>

using namespace Spark;

class SceneManagerTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {
        // 在整个测试套件开始前执行一次
    }

    static void TearDownTestSuite() {
        // 在整个测试套件结束后执行一次

    }

    void SetUp() override {
        // 在每个测试用例开始前执行
        context.SetupComponentsEvents<Hierarchy>();
        sceneManager = eastl::make_unique<SceneManager>(context);
        sceneManager->Initialize();
    }

    void TearDown() override {
        // 在每个测试用例结束后执行
        sceneManager->ShutDown();
        sceneManager.reset();
        context.Clear();
    }

    eastl::unique_ptr<SceneManager> sceneManager;
    WorldContext context;
};

void CheckHierarchy(WorldContext& context, Entity entity, Entity p, Entity f, Entity prev, Entity next)
{
    ASSERT_TRUE(context.Has<Hierarchy>(entity));
    auto hier = context.Get<Hierarchy>(entity);
    EXPECT_EQ(hier.parent, p);
    EXPECT_EQ(hier.firstChild, f);
    EXPECT_EQ(hier.prevSibling, prev);
    EXPECT_EQ(hier.nextSibling, next);
}


TEST_F(SceneManagerTest, AddAndRemove)
{
    eastl::array<Entity, 3> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    EXPECT_EQ(scene->GetEntityCount(), 0);

    scene->AddEntity(entities[0]);
    scene->AddEntity(entities[1]);
    scene->AddEntity(entities[2]);
    EXPECT_EQ(scene->GetEntityCount(), 3);

    scene->RemoveEntity(entities[0]);
    EXPECT_EQ(scene->GetEntityCount(), 2);

    context.DestoryEntity(entities[1]);
    context.DestoryEntity(entities[2]);
    EXPECT_EQ(scene->GetEntityCount(), 0);
}

TEST_F(SceneManagerTest, Contian)
{
    auto ent = context.CreateEntity();
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();

    EXPECT_FALSE(scene->Contain(ent));

    scene->AddEntity(ent);
    EXPECT_TRUE(scene->Contain(ent));

    scene->RemoveEntity(ent);
    EXPECT_FALSE(scene->Contain(ent));
}

TEST_F(SceneManagerTest, HierarchyComponentConstruct)
{
    eastl::array<Entity, 3> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];

    // invalid component
    Hierarchy invalid;
    invalid.parent = ent0;
    context.Add<Hierarchy>(ent1, invalid);
    EXPECT_EQ(scene->GetEntityCount(), 0);
    EXPECT_FALSE(scene->Contain(ent0));
    EXPECT_FALSE(scene->Contain(ent1));
    context.Remove<Hierarchy>(ent1);

    // valid component
    Hierarchy com0;
    context.Add<Hierarchy>(ent0, com0);
    Hierarchy com1;
    com1.parent = ent0;
    context.Add<Hierarchy>(ent1, com1);
    EXPECT_EQ(scene->GetEntityCount(), 2);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, NullEntity, NullEntity);

    // invalid conponent
    Hierarchy invalidCom2;
    invalidCom2.parent = ent0;
    context.Add<Hierarchy>(ent2, invalidCom2);
    EXPECT_EQ(scene->GetEntityCount(), 2);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_FALSE(scene->Contain(ent2));
    context.Remove<Hierarchy>(ent2);

    Hierarchy com2;
    com2.parent = ent0;
    com2.nextSibling = ent1;
    context.Add<Hierarchy>(ent2, com2);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, ent2, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, ent1);

    context.Remove<Hierarchy>(ent2);
    EXPECT_EQ(scene->GetEntityCount(), 2);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_FALSE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, NullEntity, NullEntity);

    com2.parent = ent1;
    com2.nextSibling = NullEntity;
    context.Add<Hierarchy>(ent2, com2);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, NullEntity, NullEntity);
}

TEST_F(SceneManagerTest, HierarchyComponentUpdate)
{
    eastl::array<Entity, 3> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];

    Hierarchy com0;
    context.Add<Hierarchy>(ent0, com0);
    Hierarchy com1;
    com1.parent = ent0;
    context.Add<Hierarchy>(ent1, com1);
    EXPECT_EQ(scene->GetEntityCount(), 2);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, NullEntity, NullEntity);

    Hierarchy newCom1;
    newCom1.parent = NullEntity;
    context.Repalce<Hierarchy>(ent1, newCom1);
    EXPECT_EQ(scene->GetEntityCount(), 2);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    CheckHierarchy(context, ent0, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, NullEntity, NullEntity, NullEntity);

    Hierarchy com2;
    com2.parent = ent0;
    context.Add<Hierarchy>(ent2, com2);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, NullEntity);

    Hierarchy newCom2;
    com2.parent = ent1;
    context.Repalce<Hierarchy>(ent2, com2);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, NullEntity, NullEntity);

    Hierarchy newerCom2;
    context.Repalce<Hierarchy>(ent2, newerCom2);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, NullEntity, NullEntity, NullEntity, NullEntity);
}

TEST_F(SceneManagerTest, HierarchyComponentDestory)
{
    eastl::array<Entity, 4> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];
    auto ent3 = entities[3];

    Hierarchy com0;
    context.Add<Hierarchy>(ent0, com0);
    Hierarchy com1;
    com1.parent = ent0;
    context.Add<Hierarchy>(ent1, com1);
    Hierarchy com2;
    com2.parent = ent1;
    context.Add<Hierarchy>(ent2, com2);
    Hierarchy com3;
    com3.parent = ent1;
    com3.prevSibling = ent2;
    context.Add<Hierarchy>(ent3, com3);
    EXPECT_EQ(scene->GetEntityCount(), 4);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    EXPECT_TRUE(scene->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, NullEntity, ent3);
    CheckHierarchy(context, ent3, ent1, NullEntity, ent2, NullEntity);

    context.Remove<Hierarchy>(ent1);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent2));
    EXPECT_TRUE(scene->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, ent3);
    CheckHierarchy(context, ent3, ent0, NullEntity, ent2, NullEntity);

    context.Remove<Hierarchy>(ent3);
    EXPECT_EQ(scene->GetEntityCount(), 2);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, NullEntity);
}

TEST_F(SceneManagerTest, SetParent)
{
    eastl::array<Entity, 4> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];
    auto ent3 = entities[3];

    scene->SetParent(ent1, ent0);
    scene->SetParent(ent2, ent1);
    scene->SetParent(ent3, ent1);
    EXPECT_EQ(scene->GetEntityCount(), 4);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent1));
    EXPECT_TRUE(scene->Contain(ent2));
    EXPECT_TRUE(scene->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, ent3, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, ent3, NullEntity);
    CheckHierarchy(context, ent3, ent1, NullEntity, NullEntity, ent2);

    scene->RemoveEntity(ent1);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent2));
    EXPECT_TRUE(scene->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent3, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, ent3, NullEntity);
    CheckHierarchy(context, ent3, ent0, NullEntity, NullEntity, ent2);

    scene->SetParent(ent3, ent2);
    EXPECT_EQ(scene->GetEntityCount(), 3);
    EXPECT_TRUE(scene->Contain(ent0));
    EXPECT_TRUE(scene->Contain(ent2));
    EXPECT_TRUE(scene->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, ent3, NullEntity, NullEntity);
    CheckHierarchy(context, ent3, ent2, NullEntity, NullEntity, NullEntity);
}

/**
 * @brief Test entity hierarchy
 * 
 *
 *          1            9       11      0
 *        /   \          |
 *       2     3        10
 *     / | \   | \
 *    4  5  6  7  8
 */
TEST_F(SceneManagerTest, Query)
{
    eastl::array<Entity, 12> ents;
    context.CreateEntity(ents.begin(), ents.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    
    scene->SetParent(ents[2], ents[1]);
    scene->SetParent(ents[3], ents[1], ents[2]);
    scene->SetParent(ents[4], ents[2]);
    scene->SetParent(ents[5], ents[2], ents[4]);
    scene->SetParent(ents[6], ents[2], ents[5]);
    scene->SetParent(ents[7], ents[3]);
    scene->SetParent(ents[8], ents[3], ents[7]);
    scene->SetParent(ents[10], ents[9]);
    scene->AddEntity(ents[0]);
    scene->AddEntity(ents[11]);

    EXPECT_EQ(scene->GetEntityCount(), 12);

    eastl::vector<Entity> path = scene->GetHierarchyPath(ents[4]);
    EXPECT_EQ(path.size(), 2);
    EXPECT_EQ(path[0], ents[1]);
    EXPECT_EQ(path[1], ents[2]);
    eastl::vector<Entity> path2 = scene->GetHierarchyPath(ents[8]);
    EXPECT_EQ(path2.size(), 2);
    EXPECT_EQ(path2[0], ents[1]);
    EXPECT_EQ(path2[1], ents[3]);

    EXPECT_TRUE(scene->IsAncestor(ents[5], ents[1]));
    EXPECT_TRUE(scene->IsAncestor(ents[7], ents[3]));
    EXPECT_TRUE(scene->IsAncestor(ents[10], ents[9]));
    EXPECT_FALSE(scene->IsAncestor(ents[11], ents[0]));
    EXPECT_FALSE(scene->IsAncestor(ents[7], ents[2]));

    EXPECT_EQ(scene->GetEntityRoot(ents[5]), ents[1]);
    EXPECT_EQ(scene->GetEntityRoot(ents[6]), ents[1]);
    EXPECT_EQ(scene->GetEntityRoot(ents[10]), ents[9]);
    EXPECT_EQ(scene->GetEntityRoot(ents[11]), ents[11]);

    eastl::vector<Entity> roots = scene->GetRootEntities();
    EXPECT_EQ(roots.size(), 4);
    //EXPECT_TRUE(roots.contains(ents[1]));
    //EXPECT_TRUE(roots.contains(ents[9]));
    //EXPECT_TRUE(roots.contains(ents[11]));
    //EXPECT_TRUE(roots.contains(ents[0]));

    eastl::vector<Entity> children = scene->GetChildren(ents[1]);
    EXPECT_EQ(children.size(), 2);
    EXPECT_EQ(children[0], ents[2]);
    EXPECT_EQ(children[1], ents[3]);
    eastl::vector<Entity> children2 = scene->GetChildren(ents[2]);
    EXPECT_EQ(children2.size(), 3);
    EXPECT_EQ(children2[0], ents[4]);
    EXPECT_EQ(children2[1], ents[5]);
    EXPECT_EQ(children2[2], ents[6]);
    eastl::vector<Entity> children3 = scene->GetChildren(ents[9]);
    EXPECT_EQ(children3.size(), 1);
    EXPECT_EQ(children3[0], ents[10]);
    eastl::vector<Entity> empty = scene->GetChildren(ents[11]);
    EXPECT_EQ(empty.size(), 0);

    EXPECT_EQ(scene->GetDepth(ents[0]), 0);
    EXPECT_EQ(scene->GetDepth(ents[1]), 0);
    EXPECT_EQ(scene->GetDepth(ents[2]), 1);
    EXPECT_EQ(scene->GetDepth(ents[7]), 2);

    eastl::vector<eastl::pair<Entity, uint32_t>> tree = scene->GetEntityTree();
    EXPECT_EQ(tree.size(), 12);

    auto GetSpace = [](uint32_t num)
    {
        eastl::string res = "";
        while(num--)
        {
            res += " ";
        }
        return res;
    };

    for (auto item: tree)
    {
        eastl::string space = GetSpace(item.second);
        LOG_INFO("{}{}", space, uint32_t(item.first));
    }
}

TEST_F(SceneManagerTest, Patch)
{
    eastl::array<Entity, 4> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IScene>::Get());
    auto scene = Service<IScene>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];
    auto ent3 = entities[3];

    scene->SetParent(ent1, ent0);
    scene->SetParent(ent2, ent1);
    scene->SetParent(ent3, ent1);

    scene->PatchEntityHierarchy(ent1, [&](Entity entity){
        context.Add<Name>(entity, eastl::to_string(uint32_t(entity)));
    });

    EXPECT_EQ(context.Get<Name>(ent1).m_name, eastl::to_string(uint32_t(ent1)));
    EXPECT_EQ(context.Get<Name>(ent2).m_name, eastl::to_string(uint32_t(ent2)));
    EXPECT_EQ(context.Get<Name>(ent3).m_name, eastl::to_string(uint32_t(ent3)));
}



