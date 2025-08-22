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

// Test VSSDataType parsing from string
TEST_F(VSSTypesTest, ParseDataTypeFromString) {
    EXPECT_EQ(vss_datatype_from_string("int8"), VSSDataType::Int8);
    EXPECT_EQ(vss_datatype_from_string("int16"), VSSDataType::Int16);
    EXPECT_EQ(vss_datatype_from_string("int32"), VSSDataType::Int32);
    EXPECT_EQ(vss_datatype_from_string("int64"), VSSDataType::Int64);
    EXPECT_EQ(vss_datatype_from_string("uint8"), VSSDataType::UInt8);
    EXPECT_EQ(vss_datatype_from_string("uint16"), VSSDataType::UInt16);
    EXPECT_EQ(vss_datatype_from_string("uint32"), VSSDataType::UInt32);
    EXPECT_EQ(vss_datatype_from_string("uint64"), VSSDataType::UInt64);
    EXPECT_EQ(vss_datatype_from_string("float"), VSSDataType::Float);
    EXPECT_EQ(vss_datatype_from_string("double"), VSSDataType::Double);
    EXPECT_EQ(vss_datatype_from_string("boolean"), VSSDataType::Boolean);
    EXPECT_EQ(vss_datatype_from_string("string"), VSSDataType::String);
    EXPECT_EQ(vss_datatype_from_string("struct"), VSSDataType::Struct);
    EXPECT_EQ(vss_datatype_from_string("array"), VSSDataType::Array);
    
    // Default case
    EXPECT_EQ(vss_datatype_from_string("unknown"), VSSDataType::Unknown);
}

// Test integer conversions
TEST_F(VSSTypesTest, IntegerConversions) {
    // Test int64_t to various VSS types
    std::variant<int64_t, double, std::string> int_value = int64_t(42);
    
    auto vss_int8 = VSSTypeHelper::from_typed_value(int_value, VSSDataType::Int8);
    EXPECT_TRUE(std::holds_alternative<int8_t>(vss_int8));
    EXPECT_EQ(std::get<int8_t>(vss_int8), 42);
    
    auto vss_uint32 = VSSTypeHelper::from_typed_value(int_value, VSSDataType::UInt32);
    EXPECT_TRUE(std::holds_alternative<uint32_t>(vss_uint32));
    EXPECT_EQ(std::get<uint32_t>(vss_uint32), 42);
    
    auto vss_double = VSSTypeHelper::from_typed_value(int_value, VSSDataType::Double);
    EXPECT_TRUE(std::holds_alternative<double>(vss_double));
    EXPECT_DOUBLE_EQ(std::get<double>(vss_double), 42.0);
}

// Test double conversions
TEST_F(VSSTypesTest, DoubleConversions) {
    std::variant<int64_t, double, std::string> double_value = 3.14159;
    
    auto vss_float = VSSTypeHelper::from_typed_value(double_value, VSSDataType::Float);
    EXPECT_TRUE(std::holds_alternative<float>(vss_float));
    EXPECT_FLOAT_EQ(std::get<float>(vss_float), 3.14159f);
    
    auto vss_int = VSSTypeHelper::from_typed_value(double_value, VSSDataType::Int32);
    EXPECT_TRUE(std::holds_alternative<int32_t>(vss_int));
    EXPECT_EQ(std::get<int32_t>(vss_int), 3);
}

// Test string conversions
TEST_F(VSSTypesTest, StringConversions) {
    std::variant<int64_t, double, std::string> string_value = std::string("test");
    
    auto vss_string = VSSTypeHelper::from_typed_value(string_value, VSSDataType::String);
    EXPECT_TRUE(std::holds_alternative<std::string>(vss_string));
    EXPECT_EQ(std::get<std::string>(vss_string), "test");
}

// Test boolean conversions
TEST_F(VSSTypesTest, BooleanConversions) {
    // From integer
    std::variant<int64_t, double, std::string> true_int = int64_t(1);
    auto vss_bool_true = VSSTypeHelper::from_typed_value(true_int, VSSDataType::Boolean);
    EXPECT_TRUE(std::holds_alternative<bool>(vss_bool_true));
    EXPECT_TRUE(std::get<bool>(vss_bool_true));
    
    std::variant<int64_t, double, std::string> false_int = int64_t(0);
    auto vss_bool_false = VSSTypeHelper::from_typed_value(false_int, VSSDataType::Boolean);
    EXPECT_TRUE(std::holds_alternative<bool>(vss_bool_false));
    EXPECT_FALSE(std::get<bool>(vss_bool_false));
}

// Test array support
TEST_F(VSSTypesTest, ArrayTypes) {
    VSSArray arr;
    arr.element_type = VSSDataType::Int32;
    arr.elements.push_back(VSSInt32(1));
    arr.elements.push_back(VSSInt32(2));
    arr.elements.push_back(VSSInt32(3));
    arr.elements.push_back(VSSInt32(4));
    arr.elements.push_back(VSSInt32(5));
    
    VSSValue array_value = arr;
    EXPECT_TRUE(std::holds_alternative<VSSArray>(array_value));
    
    auto& array = std::get<VSSArray>(array_value);
    EXPECT_EQ(array.elements.size(), 5);
    EXPECT_EQ(array.element_type, VSSDataType::Int32);
    EXPECT_TRUE(std::holds_alternative<VSSInt32>(array.elements[0]));
    EXPECT_EQ(std::get<VSSInt32>(array.elements[0]), 1);
    EXPECT_EQ(std::get<VSSInt32>(array.elements[4]), 5);
}

// Test JSON serialization
TEST_F(VSSTypesTest, JSONSerialization) {
    // Test integer
    VSSValue int_val = int32_t(42);
    EXPECT_EQ(VSSTypeHelper::to_json(int_val), "42");
    
    // Test double
    VSSValue double_val = 3.14;
    std::string json_double = VSSTypeHelper::to_json(double_val);
    EXPECT_TRUE(json_double.find("3.14") != std::string::npos);
    
    // Test string
    VSSValue string_val = std::string("hello");
    EXPECT_EQ(VSSTypeHelper::to_json(string_val), "\"hello\"");
    
    // Test boolean
    VSSValue bool_val = true;
    EXPECT_EQ(VSSTypeHelper::to_json(bool_val), "true");
}

// Test struct value handling
TEST_F(VSSTypesTest, StructValues) {
    VSSStruct struct_val;
    struct_val.type_name = "TestStruct";
    struct_val.fields["field1"] = VSSDouble(42.0);
    struct_val.fields["field2"] = VSSString("test");
    struct_val.fields["field3"] = VSSBoolean(true);
    
    EXPECT_EQ(struct_val.fields.size(), 3);
    
    auto field1 = struct_val.fields.find("field1");
    ASSERT_NE(field1, struct_val.fields.end());
    EXPECT_TRUE(std::holds_alternative<VSSDouble>(field1->second));
    EXPECT_DOUBLE_EQ(std::get<VSSDouble>(field1->second), 42.0);
    
    // Test as VSSValue
    VSSValue struct_value = struct_val;
    EXPECT_TRUE(std::holds_alternative<VSSStruct>(struct_value));
}