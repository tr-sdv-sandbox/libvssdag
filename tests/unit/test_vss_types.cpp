#include <gtest/gtest.h>
#include "vssdag/vss_types.h"

using namespace vssdag;

class VSSTypesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }
    
    void TearDown() override {
        // Cleanup code if needed
    }
};

// Test ValueType parsing from string
TEST_F(VSSTypesTest, ParseDataTypeFromString) {
    EXPECT_EQ(value_type_from_string("int32"), ValueType::INT32);
    EXPECT_EQ(value_type_from_string("int64"), ValueType::INT64);
    EXPECT_EQ(value_type_from_string("uint32"), ValueType::UINT32);
    EXPECT_EQ(value_type_from_string("uint64"), ValueType::UINT64);
    EXPECT_EQ(value_type_from_string("float"), ValueType::FLOAT);
    EXPECT_EQ(value_type_from_string("double"), ValueType::DOUBLE);
    EXPECT_EQ(value_type_from_string("bool"), ValueType::BOOL);
    EXPECT_EQ(value_type_from_string("boolean"), ValueType::BOOL);
    EXPECT_EQ(value_type_from_string("string"), ValueType::STRING);
    EXPECT_EQ(value_type_from_string("struct"), ValueType::STRUCT);

    // Default case - returns std::nullopt
    EXPECT_FALSE(value_type_from_string("unknown").has_value());
}

// Test integer conversions
TEST_F(VSSTypesTest, IntegerConversions) {
    // Test int64_t to various VSS types
    Value int_value = int64_t(42);

    auto vss_int32 = VSSTypeHelper::from_typed_value(int_value, ValueType::INT32);
    EXPECT_TRUE(std::holds_alternative<int32_t>(vss_int32));
    EXPECT_EQ(std::get<int32_t>(vss_int32), 42);

    auto vss_uint32 = VSSTypeHelper::from_typed_value(int_value, ValueType::UINT32);
    EXPECT_TRUE(std::holds_alternative<uint32_t>(vss_uint32));
    EXPECT_EQ(std::get<uint32_t>(vss_uint32), 42);

    auto vss_double = VSSTypeHelper::from_typed_value(int_value, ValueType::DOUBLE);
    EXPECT_TRUE(std::holds_alternative<double>(vss_double));
    EXPECT_DOUBLE_EQ(std::get<double>(vss_double), 42.0);
}

// Test double conversions
TEST_F(VSSTypesTest, DoubleConversions) {
    Value double_value = 3.14159;

    auto vss_float = VSSTypeHelper::from_typed_value(double_value, ValueType::FLOAT);
    EXPECT_TRUE(std::holds_alternative<float>(vss_float));
    EXPECT_FLOAT_EQ(std::get<float>(vss_float), 3.14159f);

    auto vss_int = VSSTypeHelper::from_typed_value(double_value, ValueType::INT32);
    EXPECT_TRUE(std::holds_alternative<int32_t>(vss_int));
    EXPECT_EQ(std::get<int32_t>(vss_int), 3);
}

// Test string conversions
TEST_F(VSSTypesTest, StringConversions) {
    Value string_value = std::string("test");

    auto vss_string = VSSTypeHelper::from_typed_value(string_value, ValueType::STRING);
    EXPECT_TRUE(std::holds_alternative<std::string>(vss_string));
    EXPECT_EQ(std::get<std::string>(vss_string), "test");
}

// Test boolean conversions
TEST_F(VSSTypesTest, BooleanConversions) {
    // From integer
    Value true_int = int64_t(1);
    auto vss_bool_true = VSSTypeHelper::from_typed_value(true_int, ValueType::BOOL);
    EXPECT_TRUE(std::holds_alternative<bool>(vss_bool_true));
    EXPECT_TRUE(std::get<bool>(vss_bool_true));

    Value false_int = int64_t(0);
    auto vss_bool_false = VSSTypeHelper::from_typed_value(false_int, ValueType::BOOL);
    EXPECT_TRUE(std::holds_alternative<bool>(vss_bool_false));
    EXPECT_FALSE(std::get<bool>(vss_bool_false));
}

// Test array support
TEST_F(VSSTypesTest, ArrayTypes) {
    std::vector<int32_t> arr = {1, 2, 3, 4, 5};

    Value array_value = arr;
    EXPECT_TRUE(std::holds_alternative<std::vector<int32_t>>(array_value));

    auto& array = std::get<std::vector<int32_t>>(array_value);
    EXPECT_EQ(array.size(), 5);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[4], 5);
}

// Test JSON serialization
TEST_F(VSSTypesTest, JSONSerialization) {
    // Test integer
    Value int_val = int32_t(42);
    EXPECT_EQ(VSSTypeHelper::to_json(int_val), "42");

    // Test double
    Value double_val = 3.14;
    std::string json_double = VSSTypeHelper::to_json(double_val);
    EXPECT_TRUE(json_double.find("3.14") != std::string::npos);

    // Test string
    Value string_val = std::string("hello");
    EXPECT_EQ(VSSTypeHelper::to_json(string_val), "\"hello\"");

    // Test boolean
    Value bool_val = true;
    EXPECT_EQ(VSSTypeHelper::to_json(bool_val), "true");
}

// Test struct value handling
TEST_F(VSSTypesTest, StructValues) {
    auto struct_val = std::make_shared<StructValue>();
    struct_val->set_type_name("TestStruct");
    struct_val->set_field("field1", 42.0);
    struct_val->set_field("field2", std::string("test"));
    struct_val->set_field("field3", true);

    EXPECT_EQ(struct_val->fields().size(), 3);

    auto field1_val = struct_val->get_field("field1");
    ASSERT_NE(field1_val, nullptr);
    EXPECT_TRUE(std::holds_alternative<double>(*field1_val));
    EXPECT_DOUBLE_EQ(std::get<double>(*field1_val), 42.0);

    // Test as Value
    Value struct_value = struct_val;
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<StructValue>>(struct_value));
}