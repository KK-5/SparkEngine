
#include <EASTL/string.h>
#include <EASTL/unordered_set.h>

#include <gtest/gtest.h>


TEST(EASTLTest, SimpleStringTest)
{
    eastl::string s = "Hello World";

    EXPECT_EQ(s, "Hello World");
}

struct Node
{
    int m_id;
};


TEST(EASTLTest, unordered_set)
{
    eastl::unordered_set<int> set;

    int node1, node2;
    node1 = 1;
    node2 = 2;

    set.emplace(node1);
    set.emplace(node2);
}
