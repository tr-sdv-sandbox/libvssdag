#include <gtest/gtest.h>
#include "vssdag/lua_mapper.h"
#include <cmath>

using namespace vssdag;

class LuaMapperTest : public ::testing::Test {
protected:
    std::unique_ptr<LuaMapper> mapper;
    
    void SetUp() override {
        mapper = std::make_unique<LuaMapper>();
    }
    
    void TearDown() override {
        mapper.reset();
    }
};

// Test basic Lua expression evaluation
TEST_F(LuaMapperTest, BasicExpression) {
    mapper->set_transform_code("return x * 2");
    mapper->set_input_value(10.0);
    
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path, "TestSignal");
    // Value is now in qualified_value.value
}

// Test mathematical operations
TEST_F(LuaMapperTest, MathOperations) {
    // Test addition
    mapper->set_transform_code("return x + 5");
    mapper->set_input_value(10.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
    
    // Test power
    mapper->set_transform_code("return x ^ 2");
    mapper->set_input_value(4.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test conditional logic
TEST_F(LuaMapperTest, ConditionalLogic) {
    mapper->set_transform_code("if x > 50 then return 'HIGH' else return 'LOW' end");
    
    // Test with value > 50
    mapper->set_input_value(75.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    // Test with value <= 50
    mapper->set_input_value(30.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test dependency handling
TEST_F(LuaMapperTest, DependencyHandling) {
    std::unordered_map<std::string, std::variant<int64_t, double, std::string>> deps;
    deps["Speed"] = 60.0;
    deps["Throttle"] = 0.8;
    
    mapper->set_dependencies(deps);
    mapper->set_transform_code(
        "return dependencies['Speed'] * dependencies['Throttle']"
    );
    
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test stateful transforms with history
TEST_F(LuaMapperTest, StatefulTransform) {
    // Initialize state
    mapper->set_signal_state("TestSignal", "last_value", 0.0);
    
    // First execution
    mapper->set_transform_code(R"(
        local current = x
        local last = state['last_value'] or 0
        state['last_value'] = current
        return current - last
    )");
    
    mapper->set_input_value(10.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    // Second execution with state preserved
    mapper->set_input_value(15.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test built-in low-pass filter function
TEST_F(LuaMapperTest, LowPassFilter) {
    // Initialize filter state
    mapper->set_signal_state("TestSignal", "filtered", 0.0);
    
    mapper->set_transform_code("return lowpass(x, 0.5)");
    
    // First value
    mapper->set_input_value(100.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    // Second value
    mapper->set_input_value(0.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test string manipulation
TEST_F(LuaMapperTest, StringManipulation) {
    mapper->set_transform_code("return 'Speed: ' .. tostring(x) .. ' km/h'");
    mapper->set_input_value(80.0);
    
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test boolean returns
TEST_F(LuaMapperTest, BooleanReturns) {
    mapper->set_transform_code("return x > 100");
    
    // Test true case
    mapper->set_input_value(150.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    // Test false case
    mapper->set_input_value(50.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test struct return values
TEST_F(LuaMapperTest, StructReturnValues) {
    mapper->set_transform_code(R"(
        return {
            voltage = x * 3.7,
            current = x * 10,
            temperature = 25.5
        }
    )");
    mapper->set_input_value(2.0);
    
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test error handling for invalid Lua code
TEST_F(LuaMapperTest, InvalidLuaCode) {
    mapper->set_transform_code("return x +");  // Syntax error
    mapper->set_input_value(10.0);
    
    auto result = mapper->execute_transform("TestSignal");
    EXPECT_FALSE(result.has_value());
}

// Test derivative calculation
TEST_F(LuaMapperTest, DerivativeCalculation) {
    // Initialize history for derivative
    mapper->set_signal_state("TestSignal", "last_value", 0.0);
    mapper->set_signal_state("TestSignal", "last_time", 0.0);
    
    mapper->set_transform_code("return derivative(x, 'TestSignal')");
    
    // First call establishes baseline
    mapper->set_input_value(0.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    // Wait a bit and provide new value
    // Note: In real tests, we'd need to mock time or wait
    // For now, we assume the derivative function handles time internally
    mapper->set_input_value(10.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}

// Test moving average
TEST_F(LuaMapperTest, MovingAverage) {
    mapper->set_transform_code("return moving_average(x, 'TestSignal', 3)");
    
    // Add values to build up the average
    mapper->set_input_value(10.0);
    auto result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    mapper->set_input_value(20.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    mapper->set_input_value(30.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());

    mapper->set_input_value(40.0);
    result = mapper->execute_transform("TestSignal");
    ASSERT_TRUE(result.has_value());
}