#include <vector>
#include <iostream>
#include <gtest/gtest.h>

#include <EASTL/string.h>

#include <HashString/HashString.h>
#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Log/SpdLogSystem.h>
#include <ECS/WorldContext.h>
#include <Serialization/UIElement.h>

#include <Math/Vector2.h>
#include <Math/Vector3.h>
#include <Math/Vector4.h>
#include <Math/Matrix3X3.h>
#include <Math/Matrix4x4.h>
#include <Math/Quaternion.h>

using namespace Spark;

class Foo
{
public:
    Foo():x(1), y(2.f), vec({1, 2, 3}){}
    ~Foo() = default;

    float Sum()
    {
        return (float)x + y;
    }

    int x;
    float y;
    std::vector<int> vec;
};

TEST(ReflectionTest, Class) {
    Foo foo;
    ReflectContext context;
    context.Reflect<Foo>().Type("Foo"_hs, "Foo")
        .Data<&Foo::x>("x"_hs, "x")
        .Data<&Foo::y>("y"_hs, "y")
        .Data<&Foo::vec>("vec"_hs, "vec");

    MetaType type = context.Resolve("Foo"_hs);
    ASSERT_TRUE(type.is_class());
    ASSERT_EQ(type.size_of(), sizeof(Foo));
    ASSERT_TRUE(context.Resolve(GetTypeInfo<Foo>()));
    
    MetaData data = type.data("x"_hs);
    ASSERT_EQ(data.get(foo).cast<int>(), 1);
    ASSERT_TRUE(data.set(foo, 2));
    ASSERT_EQ(data.get(foo).cast<int>(), 2);

    ASSERT_TRUE(type.data("y"_hs));
    ASSERT_FALSE(type.data("y"_hs).type().is_class());
    ASSERT_EQ(type.data("y"_hs).type().size_of(), sizeof(float));
    ASSERT_EQ(type.data("y"_hs).type().info().name(), "float");
    ASSERT_EQ(type.data("y"_hs).get(foo).cast<float>(), 2.f);
    ASSERT_TRUE(type.data("y"_hs).set(foo, 5.f));
    ASSERT_EQ(type.data("y"_hs).get(foo).cast<float>(), 5.f);
}

TEST(ReflectionTest, ClassFunc) {
    Foo foo;
    ReflectContext context;
    context.Reflect<Foo>().Type("Foo"_hs, "Foo")
        .Func<&Foo::Sum>("sum"_hs, "sum");

    MetaFunc func = context.Resolve("Foo"_hs).func("sum"_hs);
    ASSERT_EQ(func.invoke(foo).cast<float>(), 3.f);
}

TEST(ReflectionTest, ClassContainer) {
    Foo foo;
    ReflectContext context;
    context.Reflect<Foo>().Type("Foo"_hs, "Foo")
        .Data<&Foo::vec>("vec"_hs, "vec");

    MetaData data= context.Resolve<Foo>().data("vec"_hs);
    MetaAny any = data.get(foo);
    ASSERT_TRUE(any.as_sequence_container());
    auto view = any.as_sequence_container();
    
    ASSERT_EQ(view[0].cast<int>(), 1);
    ASSERT_EQ(view[1].cast<int>(), 2);
    ASSERT_EQ(view[2].cast<int>(), 3);
    
    view[2].cast<int&>() = 4;
    ASSERT_EQ(view[2].cast<int>(), 4);
}

TEST(ReflectionTest, ClassPrint) {
    Foo foo;
    ReflectContext context;
    context.Reflect<Foo>().Type("Foo"_hs, "Foo")
        .Data<&Foo::x>("x"_hs, "x")
        .Data<&Foo::y>("y"_hs, "y")
        .Data<&Foo::vec>("vec"_hs, "vec");

    MetaType type = context.Resolve<decltype(foo)>();
    ASSERT_EQ(type.id(), "Foo"_hs);
    ASSERT_STREQ(type.name(), "Foo");

    std::cout << type.name() << ":\n";
    for ([[maybe_unused]] auto cur : type.data())
    {
        std::cout << "    ";
        MetaData data = cur.second;
        std::cout << cur.first << " " << data.name() << ": ";
        if (data.get(foo).try_cast<int>())
        {
            std::cout << data.get(foo).cast<int>();
        }
        else if (data.get(foo).try_cast<float>())
        {
            std::cout << data.get(foo).cast<float>();
        }

        if (data.get(foo).type().is_sequence_container())
        {
            // view 和 view1 不一样？
            MetaAny v = data.get(foo);
            auto view = v.as_sequence_container();

            auto view1 = data.get(foo).as_sequence_container();

            for (auto value : view)
            {
                std::cout << value.cast<int>() << " ";
            }
        }
        std::cout << "\n";
    }
}

TEST(ReflectionTest, Context) {
    Foo foo;
    ReflectContext context;
    context.Reflect<Foo>().Type("Foo"_hs, "Foo")
        .Data<&Foo::x>("x"_hs, "x")
        .Data<&Foo::y>("y"_hs, "y")
        .Data<&Foo::vec>("vec"_hs, "vec");

    ReflectContext otherContext;
    otherContext.Reflect<Foo>().Type("Foo2"_hs, "Foo2")
        .Data<&Foo::x>("x2"_hs, "x2")
        .Data<&Foo::y>("y2"_hs, "y2")
        .Data<&Foo::vec>("vec2"_hs, "vec2");

    ASSERT_NE(context.Resolve<Foo>().id(), otherContext.Resolve<Foo>().id());
    ASSERT_TRUE(context.Resolve<Foo>().data("x"_hs).set(foo, 6));
    ASSERT_EQ(context.Resolve<Foo>().data("x"_hs).get(foo).cast<int>(), 6);
    ASSERT_EQ(otherContext.Resolve<Foo>().data("x2"_hs).get(foo).cast<int>(), 6);

    context.Reset<Foo>();
    ASSERT_FALSE(context.Resolve("Foo"_hs));
    ASSERT_TRUE(otherContext.Resolve("Foo2"_hs));
}

struct TestName
{
    eastl::string name;
};

struct Position
{
    int x, y;
};

static void Reflect(ReflectContext& context)
{
    context.Reflect<TestName>()
        .Type("TestName")
        .Data<&TestName::name>("TestName::name"_hs, "name")
        .Func<&WorldContext::Has<TestName>>("HasComponent")
        .Func<&WorldContext::TryGet<TestName>>("GetComponent");
    
    context.Reflect<Position>()
        .Type("Position")
        .Data<&Position::x>("Position::x"_hs, "x")
        .Data<&Position::y>("Position::y"_hs, "y")
        .Func<&WorldContext::Has<Position>>("HasComponent")
        .Func<&WorldContext::TryGet<Position>>("GetComponent");
}

bool Com(const MetaType& first, const MetaType& second)
{
    return eastl::string(first.name()) < eastl::string(second.name());
}

TEST(ReflectionTest, TypeRegistry)
{
    TypeRegistry::Register(Reflect);
    TypeRegistry::RegisterAll();

    ReflectContext& context = TypeRegistry::GetContext();
    EXPECT_EQ(context.TypeSize(), 2);

    eastl::vector<MetaType> types = context.GetAllTypes(Com);
    EXPECT_EQ(types[0], context.Resolve<Position>());
    EXPECT_EQ(types[1], context.Resolve<TestName>());
    EXPECT_EQ(context.Resolve<Position>().info(), GetTypeInfo<Position>());

    WorldContext world;
    Entity ent = world.CreateEntity();
    world.Add<TestName>(ent, "TestEntity");
    Entity ent2 = world.CreateEntity();

    EXPECT_EQ(world.Get<TestName>(ent).name, "TestEntity");
    MetaFunc query = context.Resolve<TestName>().func("HasComponent"_hs);
    // 这里构造了一个TestName的对象作为第一个参数
    EXPECT_TRUE(query.invoke({}, AnyCast(world), ent));
    MetaAny com = context.Resolve<TestName>().func("GetComponent"_hs).invoke({}, AnyCast(world), ent);
    EXPECT_TRUE(com);
    EXPECT_TRUE(com.try_cast<const TestName*>());
    EXPECT_EQ(com.cast<const TestName*>()->name, "TestEntity");
    MetaData name = context.Resolve<TestName>().data("TestName::name"_hs);
    MetaAny value = name.get(*com);
    EXPECT_TRUE(value.try_cast<eastl::string>());

    MetaAny nullCom = context.Resolve<TestName>().func("GetComponent"_hs).invoke({}, AnyCast(world), ent2);
    auto test = world.TryGet<TestName>(ent2);
    EXPECT_FALSE(test);
    EXPECT_EQ(nullCom.cast<const TestName*>(), nullptr);
    EXPECT_FALSE(*nullCom);
    
    MetaType testName = context.Resolve<TestName>();
    MetaAny instance = testName.construct();
    EXPECT_TRUE(instance.try_cast<TestName>());
}

struct Float
{
    float value = 0;
};

static void FloatReflect(ReflectContext& context)
{
    context.Reflect<Float>()
        .Type("Float")
        .Data<&Float::value>("value").Custom<FloatElement>(0.0f, 1.0f, 0.1f);
}

TEST(ReflectionTest, Custom)
{
    TypeRegistry::Register(FloatReflect);
    TypeRegistry::RegisterAll();

    ReflectContext& context = TypeRegistry::GetContext();

    MetaData value = context.Resolve<Float>().data("value"_hs);
    MetaCustom elem = value.custom();
    EXPECT_NE(static_cast<FloatElement*>(elem), nullptr);

    FloatElement fe = *static_cast<FloatElement*>(elem);
    EXPECT_FLOAT_EQ(fe.min, 0.0f);
    EXPECT_FLOAT_EQ(fe.max, 1.0f);
    EXPECT_FLOAT_EQ(fe.speed, 0.1f);
    EXPECT_STREQ(fe.format, "%.3f");

}