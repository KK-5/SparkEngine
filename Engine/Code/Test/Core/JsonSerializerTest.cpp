#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Reflection/ReflectContext.h>
#include <Serialization/JsonSerializer.h>
#include <Serialization/MetaFieldTraits.h>

using namespace Spark;

namespace
{
    //! Values are explicit and non-contiguous on purpose: an enumerator matched by its
    //! position in the reflected list instead of by value would map to the wrong name.
    enum class Season : uint8_t
    {
        Spring = 1,
        Summer = 4,
        Winter = 9,
    };

    struct Stage
    {
        eastl::string entry;
        int32_t       order    = 0;
        bool          optional = false;
    };

    struct Config
    {
        Season               season      = Season::Spring;
        uint64_t             bigUnsigned = 0;
        int64_t              bigSigned   = 0;
        long                 plainLong   = 0;
        unsigned char        tiny        = 0;
        float                scale       = 1.0f;
        eastl::string        name;
        Stage                main;
        eastl::vector<Stage> stages;
        uint32_t             derived     = 0;
    };

    void Reflect(ReflectContext& context)
    {
        context.Reflect<Season>()
            .Type("Season")
            .Data<Season::Spring>("Spring")
            .Data<Season::Summer>("Summer")
            .Data<Season::Winter>("Winter");

        context.Reflect<Stage>()
            .Type("Stage")
            .Data<&Stage::entry>("entry").Traits(MetaFieldTraits::Serializable)
            .Data<&Stage::order>("order").Traits(MetaFieldTraits::Serializable)
            .Data<&Stage::optional>("optional").Traits(MetaFieldTraits::Serializable);

        context.Reflect<Config>()
            .Type("Config")
            .Data<&Config::season>("season").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::bigUnsigned>("bigUnsigned").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::bigSigned>("bigSigned").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::plainLong>("plainLong").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::tiny>("tiny").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::scale>("scale").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::name>("name").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::main>("main").Traits(MetaFieldTraits::Serializable)
            .Data<&Config::stages>("stages").Traits(MetaFieldTraits::Serializable)
            // Reflected for display only, never marked -- must not reach the file.
            .Data<&Config::derived>("derived");
    }

    class JsonSerializerTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Reflect(m_context); }

        JsonValue Encode(const Config& config)
        {
            JsonValue json;
            EXPECT_TRUE(SerializeToJson(m_context.Resolve<Config>().from_void(&config), json));
            return json;
        }

        bool Decode(const JsonValue& json, Config& config)
        {
            MetaAny target = m_context.Resolve<Config>().from_void(&config);
            return DeserializeFromJson(json, target);
        }

        ReflectContext m_context;
    };
}

TEST_F(JsonSerializerTest, DefaultsAreWritten)
{
    // A field is never dropped for happening to equal a default: changing a default in
    // code would otherwise restate what files already on disk mean.
    const Config config;
    EXPECT_EQ(Encode(config).dump(),
        R"({"season":"Spring","bigUnsigned":0,"bigSigned":0,"plainLong":0,"tiny":0,)"
        R"("scale":1.0,"name":"","main":{"entry":"","order":0,"optional":false},"stages":[]})");
}

TEST_F(JsonSerializerTest, UnmarkedFieldIsSkipped)
{
    Config config;
    config.derived = 4096;

    const JsonValue json = Encode(config);
    EXPECT_FALSE(json.contains("derived"));
    EXPECT_TRUE(json.contains("season"));
}

TEST_F(JsonSerializerTest, IntegerExtremesSurviveRoundTrip)
{
    Config config;
    config.bigUnsigned = 18446744073709551615ull;
    config.bigSigned   = INT64_MIN;
    config.plainLong   = -2147483647L - 1L;
    config.tiny        = 255;

    Config decoded;
    ASSERT_TRUE(Decode(Encode(config), decoded));
    EXPECT_EQ(decoded.bigUnsigned, config.bigUnsigned);
    EXPECT_EQ(decoded.bigSigned, config.bigSigned);
    EXPECT_EQ(decoded.plainLong, config.plainLong);
    EXPECT_EQ(decoded.tiny, config.tiny);
}

TEST_F(JsonSerializerTest, EnumIsStoredByName)
{
    Config config;
    config.season = Season::Winter;
    EXPECT_EQ(Encode(config)["season"], "Winter");

    Config decoded;
    ASSERT_TRUE(Decode(Encode(config), decoded));
    EXPECT_EQ(decoded.season, Season::Winter);
}

TEST_F(JsonSerializerTest, FieldOrderFollowsRegistration)
{
    Config config;
    config.name   = "hello";
    config.season = Season::Summer;
    config.scale  = 2.0f;

    EXPECT_EQ(Encode(config).dump(),
        R"({"season":"Summer","bigUnsigned":0,"bigSigned":0,"plainLong":0,"tiny":0,)"
        R"("scale":2.0,"name":"hello","main":{"entry":"","order":0,"optional":false},"stages":[]})");
}

TEST_F(JsonSerializerTest, NestedStructRoundTrips)
{
    Config config;
    config.main.entry    = "VSMain";
    config.main.order    = 7;
    config.main.optional = true;

    EXPECT_EQ(Encode(config)["main"].dump(),
        R"({"entry":"VSMain","order":7,"optional":true})");

    Config decoded;
    ASSERT_TRUE(Decode(Encode(config), decoded));
    EXPECT_EQ(decoded.main.entry, "VSMain");
    EXPECT_EQ(decoded.main.order, 7);
    EXPECT_TRUE(decoded.main.optional);
}

TEST_F(JsonSerializerTest, VectorRoundTrips)
{
    Config config;
    config.stages.push_back(Stage{"VSMain", 1, false});
    config.stages.push_back(Stage{});
    config.stages.push_back(Stage{"PSMain", 2, true});

    // The all-default element keeps its slot, written out in full like any other.
    EXPECT_EQ(Encode(config)["stages"].dump(),
        R"([{"entry":"VSMain","order":1,"optional":false},)"
        R"({"entry":"","order":0,"optional":false},)"
        R"({"entry":"PSMain","order":2,"optional":true}])");

    Config decoded;
    ASSERT_TRUE(Decode(Encode(config), decoded));
    ASSERT_EQ(decoded.stages.size(), 3u);
    EXPECT_EQ(decoded.stages[0].entry, "VSMain");
    EXPECT_EQ(decoded.stages[1].entry, "");
    EXPECT_EQ(decoded.stages[2].order, 2);
    EXPECT_TRUE(decoded.stages[2].optional);
}

TEST_F(JsonSerializerTest, EmptyVectorRoundTrips)
{
    Config config;
    config.stages.push_back(Stage{"VSMain", 1, false});

    Config decoded;
    decoded.stages.push_back(Stage{});
    decoded.stages.push_back(Stage{});

    JsonValue json = JsonValue::object();
    json["stages"] = JsonValue::array();
    ASSERT_TRUE(Decode(json, decoded));
    EXPECT_TRUE(decoded.stages.empty());
}

TEST_F(JsonSerializerTest, MissingKeyKeepsCurrentValue)
{
    Config decoded;
    decoded.scale  = 9.0f;
    decoded.season = Season::Winter;

    JsonValue json = JsonValue::object();
    json["name"] = "only this one";

    ASSERT_TRUE(Decode(json, decoded));
    EXPECT_EQ(decoded.name, "only this one");
    EXPECT_FLOAT_EQ(decoded.scale, 9.0f);
    EXPECT_EQ(decoded.season, Season::Winter);
}

TEST_F(JsonSerializerTest, UnknownKeyIsIgnored)
{
    Config decoded;
    JsonValue json = JsonValue::object();
    json["fieldFromTheFuture"] = 12;
    json["name"]               = "kept";

    EXPECT_TRUE(Decode(json, decoded));
    EXPECT_EQ(decoded.name, "kept");
}

TEST_F(JsonSerializerTest, UnknownEnumeratorFailsWithoutLosingOtherFields)
{
    Config decoded;
    JsonValue json = JsonValue::object();
    json["season"] = "Autumn";
    json["name"]   = "still written";

    EXPECT_FALSE(Decode(json, decoded));
    EXPECT_EQ(decoded.season, Season::Spring);
    EXPECT_EQ(decoded.name, "still written");
}

TEST_F(JsonSerializerTest, WrongJsonKindFailsWithoutLosingOtherFields)
{
    Config decoded;
    JsonValue json = JsonValue::object();
    json["scale"] = "not a number";
    json["name"]  = "still written";

    EXPECT_FALSE(Decode(json, decoded));
    EXPECT_FLOAT_EQ(decoded.scale, 1.0f);
    EXPECT_EQ(decoded.name, "still written");
}

TEST_F(JsonSerializerTest, VectorIsRecognizedAsSequenceContainer)
{
    // Guards the eastl::vector meta traits: without the specialization this reports false
    // and the field would silently encode as an empty object instead of an array.
    EXPECT_TRUE(m_context.Resolve<eastl::vector<Stage>>().is_sequence_container());
}

// ---- JsonOperation ------------------------------------------------------------------

namespace
{
    //! Reflected fields AND an operation, the same shape AssetId has: the two entry points
    //! must produce different things, and the operation must be reachable from a field.
    struct Ref
    {
        eastl::string name;
        int32_t       index = 0;
    };

    //! A single string, which no field walk could ever produce -- that is what makes the
    //! assertions below able to tell which path ran.
    bool RefToJson(const Ref& ref, JsonValue& out)
    {
        if (ref.name.empty())
        {
            out = nullptr;
            return true;
        }
        out = std::string(ref.name.c_str(), ref.name.size()) + "#" + std::to_string(ref.index);
        return true;
    }

    bool RefFromJson(const JsonValue& in, Ref& target)
    {
        if (in.is_null())
        {
            target = Ref{};
            return true;
        }
        if (!in.is_string())
        {
            return false;
        }
        const std::string text  = in.get<std::string>();
        const size_t      split = text.find('#');
        if (split == std::string::npos)
        {
            return false;
        }
        target.name.assign(text.c_str(), split);
        target.index = std::stoi(text.substr(split + 1));
        return true;
    }

    struct Holder
    {
        Ref           primary;
        eastl::string label;
    };

    class JsonOperationTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // Brings in Stage, which the no-operation case below leans on.
            Reflect(m_context);

            m_context.Reflect<Ref>()
                .Type("Ref")
                .Data<&Ref::name>("name").Traits(MetaFieldTraits::Serializable)
                .Data<&Ref::index>("index").Traits(MetaFieldTraits::Serializable);
            ReflectJsonOperation<Ref, &RefToJson, &RefFromJson>(m_context);

            m_context.Reflect<Holder>()
                .Type("Holder")
                .Data<&Holder::primary>("primary").Traits(MetaFieldTraits::Serializable)
                .Data<&Holder::label>("label").Traits(MetaFieldTraits::Serializable);
        }

        ReflectContext m_context;
    };
}

TEST_F(JsonOperationTest, TakesOverAtTopLevel)
{
    const Ref ref{"alpha", 3};

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(m_context.Resolve<Ref>().from_void(&ref), json));
    EXPECT_EQ(json.dump(), R"("alpha#3")");
}

TEST_F(JsonOperationTest, TakesOverForAField)
{
    const Holder holder{Ref{"alpha", 3}, "outer"};

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(m_context.Resolve<Holder>().from_void(&holder), json));
    EXPECT_EQ(json.dump(), R"({"primary":"alpha#3","label":"outer"})");
}

TEST_F(JsonOperationTest, RoundTripsThroughAField)
{
    const Holder original{Ref{"alpha", 3}, "outer"};

    JsonValue json;
    ASSERT_TRUE(SerializeToJson(m_context.Resolve<Holder>().from_void(&original), json));

    Holder decoded;
    MetaAny target = m_context.Resolve<Holder>().from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));

    EXPECT_EQ(decoded.primary.name, "alpha");
    EXPECT_EQ(decoded.primary.index, 3);
    EXPECT_EQ(decoded.label, "outer");
}

TEST_F(JsonOperationTest, UnsetEncodesAsNullAndReadsBackUnset)
{
    JsonValue json;
    const Ref unset;
    ASSERT_TRUE(SerializeToJson(m_context.Resolve<Ref>().from_void(&unset), json));
    EXPECT_TRUE(json.is_null());

    Ref decoded{"leftover", 7};
    MetaAny target = m_context.Resolve<Ref>().from_void(&decoded);
    ASSERT_TRUE(DeserializeFromJson(json, target));
    EXPECT_TRUE(decoded.name.empty());
    EXPECT_EQ(decoded.index, 0);
}

TEST_F(JsonOperationTest, FailureDoesNotFallBackToTheGenericDispatch)
{
    // Falling back is the failure mode being guarded against: a value that reached the
    // object branch would come back missing whatever only the operation knows how to
    // carry -- for an AssetId, its descriptor.
    Ref decoded{"kept", 5};
    MetaAny target = m_context.Resolve<Ref>().from_void(&decoded);

    JsonValue json = 42;   // neither a string nor null, so RefFromJson rejects it
    EXPECT_FALSE(DeserializeFromJson(json, target));
    EXPECT_EQ(decoded.name, "kept");
    EXPECT_EQ(decoded.index, 5);
}

TEST_F(JsonOperationTest, TypeWithoutAnOperationIsUnaffected)
{
    // The lookup runs ahead of every built-in branch, so it must be invisible to types
    // that never registered one.
    JsonValue json;
    const Stage stage{"main", 2, true};
    ASSERT_TRUE(SerializeToJson(m_context.Resolve<Stage>().from_void(&stage), json));
    EXPECT_EQ(json.dump(), R"({"entry":"main","order":2,"optional":true})");
}
