#include <gtest/gtest.h>
#include <EASTL/array.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/Tag.h>
#include <Service/Service.h>
#include <Log/ILogSystem.h>

#include <Hierarchy/HierarchyComponent.h>
#include <Hierarchy/IHierarchy.h>
#include <Hierarchy/HierarchyManager.h>

using namespace Spark;

class HierarchyManagerTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {

    }

    static void TearDownTestSuite() {

    }

    void SetUp() override {
        WorldExecuteContext::Push(context);
        hierarchyManager = CreateSystem<HierarchyManager>();
        hierarchyManager->Init();
    }

    void TearDown() override {
        hierarchyManager.reset();
        WorldExecuteContext::Pop();
        context.Clear();
    }

    SystemUniquePtr<HierarchyManager> hierarchyManager;
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


TEST_F(HierarchyManagerTest, AddAndRemove)
{
    eastl::array<Entity, 3> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
    EXPECT_EQ(hierarchy->GetEntityCount(), 0);

    hierarchy->AddEntity(entities[0]);
    hierarchy->AddEntity(entities[1]);
    hierarchy->AddEntity(entities[2]);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);

    hierarchy->RemoveEntity(entities[0]);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);

    context.DestoryEntity(entities[1]);
    context.DestoryEntity(entities[2]);
    EXPECT_EQ(hierarchy->GetEntityCount(), 0);
}

TEST_F(HierarchyManagerTest, Contian)
{
    auto ent = context.CreateEntity();
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();

    EXPECT_FALSE(hierarchy->Contain(ent));

    hierarchy->AddEntity(ent);
    EXPECT_TRUE(hierarchy->Contain(ent));

    hierarchy->RemoveEntity(ent);
    EXPECT_FALSE(hierarchy->Contain(ent));
}

TEST_F(HierarchyManagerTest, HierarchyComponentConstruct)
{
    eastl::array<Entity, 3> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];

    // invalid component
    Hierarchy invalid;
    invalid.parent = ent0;
    context.Add<Hierarchy>(ent1, invalid);
    EXPECT_EQ(hierarchy->GetEntityCount(), 0);
    EXPECT_FALSE(hierarchy->Contain(ent0));
    EXPECT_FALSE(hierarchy->Contain(ent1));
    context.Remove<Hierarchy>(ent1);

    // valid component
    Hierarchy com0;
    context.Add<Hierarchy>(ent0, com0);
    Hierarchy com1;
    com1.parent = ent0;
    context.Add<Hierarchy>(ent1, com1);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, NullEntity, NullEntity);

    // invalid conponent
    Hierarchy invalidCom2;
    invalidCom2.parent = ent0;
    context.Add<Hierarchy>(ent2, invalidCom2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_FALSE(hierarchy->Contain(ent2));
    context.Remove<Hierarchy>(ent2);

    Hierarchy com2;
    com2.parent = ent0;
    com2.nextSibling = ent1;
    context.Add<Hierarchy>(ent2, com2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, ent2, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, ent1);

    context.Remove<Hierarchy>(ent2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_FALSE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, NullEntity, NullEntity);

    com2.parent = ent1;
    com2.nextSibling = NullEntity;
    context.Add<Hierarchy>(ent2, com2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, NullEntity, NullEntity);
}

TEST_F(HierarchyManagerTest, HierarchyComponentUpdate)
{
    eastl::array<Entity, 3> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];

    Hierarchy com0;
    context.Add<Hierarchy>(ent0, com0);
    Hierarchy com1;
    com1.parent = ent0;
    context.Add<Hierarchy>(ent1, com1);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, NullEntity, NullEntity, NullEntity);

    Hierarchy newCom1;
    newCom1.parent = NullEntity;
    context.Replace<Hierarchy>(ent1, newCom1);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    CheckHierarchy(context, ent0, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, NullEntity, NullEntity, NullEntity);

    Hierarchy com2;
    com2.parent = ent0;
    context.Add<Hierarchy>(ent2, com2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, NullEntity);

    Hierarchy newCom2;
    com2.parent = ent1;
    context.Replace<Hierarchy>(ent2, com2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, NullEntity, NullEntity);

    Hierarchy newerCom2;
    context.Replace<Hierarchy>(ent2, newerCom2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, NullEntity, NullEntity, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, NullEntity, NullEntity, NullEntity, NullEntity);
}

TEST_F(HierarchyManagerTest, HierarchyComponentDestory)
{
    eastl::array<Entity, 4> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
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
    EXPECT_EQ(hierarchy->GetEntityCount(), 4);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    EXPECT_TRUE(hierarchy->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, NullEntity, ent3);
    CheckHierarchy(context, ent3, ent1, NullEntity, ent2, NullEntity);

    context.Remove<Hierarchy>(ent1);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    EXPECT_TRUE(hierarchy->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, ent3);
    CheckHierarchy(context, ent3, ent0, NullEntity, ent2, NullEntity);

    context.Remove<Hierarchy>(ent3);
    EXPECT_EQ(hierarchy->GetEntityCount(), 2);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    CheckHierarchy(context, ent0, NullEntity, ent2, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, NullEntity, NullEntity);
}

TEST_F(HierarchyManagerTest, SetParent)
{
    eastl::array<Entity, 4> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];
    auto ent3 = entities[3];

    hierarchy->SetParent(ent1, ent0);
    hierarchy->SetParent(ent2, ent1);
    hierarchy->SetParent(ent3, ent1);
    EXPECT_EQ(hierarchy->GetEntityCount(), 4);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent1));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    EXPECT_TRUE(hierarchy->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent1, NullEntity, NullEntity);
    CheckHierarchy(context, ent1, ent0, ent3, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent1, NullEntity, ent3, NullEntity);
    CheckHierarchy(context, ent3, ent1, NullEntity, NullEntity, ent2);

    hierarchy->RemoveEntity(ent1);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    EXPECT_TRUE(hierarchy->Contain(ent3));
    CheckHierarchy(context, ent0, NullEntity, ent3, NullEntity, NullEntity);
    CheckHierarchy(context, ent2, ent0, NullEntity, ent3, NullEntity);
    CheckHierarchy(context, ent3, ent0, NullEntity, NullEntity, ent2);

    hierarchy->SetParent(ent3, ent2);
    EXPECT_EQ(hierarchy->GetEntityCount(), 3);
    EXPECT_TRUE(hierarchy->Contain(ent0));
    EXPECT_TRUE(hierarchy->Contain(ent2));
    EXPECT_TRUE(hierarchy->Contain(ent3));
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
TEST_F(HierarchyManagerTest, Query)
{
    eastl::array<Entity, 12> ents;
    context.CreateEntity(ents.begin(), ents.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
    
    hierarchy->SetParent(ents[2], ents[1]);
    hierarchy->SetParent(ents[3], ents[1], ents[2]);
    hierarchy->SetParent(ents[4], ents[2]);
    hierarchy->SetParent(ents[5], ents[2], ents[4]);
    hierarchy->SetParent(ents[6], ents[2], ents[5]);
    hierarchy->SetParent(ents[7], ents[3]);
    hierarchy->SetParent(ents[8], ents[3], ents[7]);
    hierarchy->SetParent(ents[10], ents[9]);
    hierarchy->AddEntity(ents[0]);
    hierarchy->AddEntity(ents[11]);

    EXPECT_EQ(hierarchy->GetEntityCount(), 12);

    eastl::vector<Entity> path = hierarchy->GetHierarchyPath(ents[4]);
    EXPECT_EQ(path.size(), 2);
    EXPECT_EQ(path[0], ents[1]);
    EXPECT_EQ(path[1], ents[2]);
    eastl::vector<Entity> path2 = hierarchy->GetHierarchyPath(ents[8]);
    EXPECT_EQ(path2.size(), 2);
    EXPECT_EQ(path2[0], ents[1]);
    EXPECT_EQ(path2[1], ents[3]);

    EXPECT_TRUE(hierarchy->IsAncestor(ents[5], ents[1]));
    EXPECT_TRUE(hierarchy->IsAncestor(ents[7], ents[3]));
    EXPECT_TRUE(hierarchy->IsAncestor(ents[10], ents[9]));
    EXPECT_FALSE(hierarchy->IsAncestor(ents[11], ents[0]));
    EXPECT_FALSE(hierarchy->IsAncestor(ents[7], ents[2]));

    EXPECT_EQ(hierarchy->GetEntityRoot(ents[5]), ents[1]);
    EXPECT_EQ(hierarchy->GetEntityRoot(ents[6]), ents[1]);
    EXPECT_EQ(hierarchy->GetEntityRoot(ents[10]), ents[9]);
    EXPECT_EQ(hierarchy->GetEntityRoot(ents[11]), ents[11]);

    eastl::vector<Entity> roots = hierarchy->GetRootEntities();
    EXPECT_EQ(roots.size(), 4);
    //EXPECT_TRUE(roots.contains(ents[1]));
    //EXPECT_TRUE(roots.contains(ents[9]));
    //EXPECT_TRUE(roots.contains(ents[11]));
    //EXPECT_TRUE(roots.contains(ents[0]));

    eastl::vector<Entity> children = hierarchy->GetChildren(ents[1]);
    ASSERT_EQ(children.size(), 2);
    EXPECT_EQ(children[0], ents[2]);
    EXPECT_EQ(children[1], ents[3]);
    eastl::vector<Entity> children2 = hierarchy->GetChildren(ents[2]);
    ASSERT_EQ(children2.size(), 3);
    EXPECT_EQ(children2[0], ents[4]);
    EXPECT_EQ(children2[1], ents[5]);
    EXPECT_EQ(children2[2], ents[6]);
    eastl::vector<Entity> children3 = hierarchy->GetChildren(ents[9]);
    ASSERT_EQ(children3.size(), 1);
    EXPECT_EQ(children3[0], ents[10]);
    eastl::vector<Entity> empty = hierarchy->GetChildren(ents[11]);
    ASSERT_EQ(empty.size(), 0);

    EXPECT_EQ(hierarchy->GetDepth(ents[0]), 0);
    EXPECT_EQ(hierarchy->GetDepth(ents[1]), 0);
    EXPECT_EQ(hierarchy->GetDepth(ents[2]), 1);
    EXPECT_EQ(hierarchy->GetDepth(ents[7]), 2);

    eastl::vector<eastl::pair<Entity, uint32_t>> tree = hierarchy->GetEntityTree();
    ASSERT_EQ(tree.size(), 12);

    auto GetSpace = [](uint32_t num)
    {
        eastl::string res = "";
        while(num--)
        {
            res += "  ";
        }
        return res;
    };

    for (auto item: tree)
    {
        eastl::string space = GetSpace(item.second);
        LOG_INFO("{}{}", space, uint32_t(item.first));
    }
}

TEST_F(HierarchyManagerTest, Patch)
{
    eastl::array<Entity, 4> entities;
    context.CreateEntity(entities.begin(), entities.end());
    ASSERT_TRUE(Service<IHierarchy>::Get());
    auto hierarchy = Service<IHierarchy>::Get();
    auto ent0 = entities[0];
    auto ent1 = entities[1];
    auto ent2 = entities[2];
    auto ent3 = entities[3];

    hierarchy->SetParent(ent1, ent0);
    hierarchy->SetParent(ent2, ent1);
    hierarchy->SetParent(ent3, ent1);

    hierarchy->PatchEntityHierarchy(ent1, [&](Entity entity){
        context.Add<Name>(entity, eastl::to_string(uint32_t(entity)));
    });

    EXPECT_EQ(context.Get<Name>(ent1).name, eastl::to_string(uint32_t(ent1)));
    EXPECT_EQ(context.Get<Name>(ent2).name, eastl::to_string(uint32_t(ent2)));
    EXPECT_EQ(context.Get<Name>(ent3).name, eastl::to_string(uint32_t(ent3)));
}



