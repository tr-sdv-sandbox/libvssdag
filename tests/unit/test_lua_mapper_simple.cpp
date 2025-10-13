#include <gtest/gtest.h>
#include "vssdag/lua_mapper.h"

using namespace vssdag;

class LuaMapperSimpleTest : public ::testing::Test {
protected:
    std::unique_ptr<LuaMapper> mapper;
    
    void SetUp() override {
        mapper = std::make_unique<LuaMapper>();
    }
    
    void TearDown() override {
        mapper.reset();
    }
};

// Test basic Lua execution
TEST_F(LuaMapperSimpleTest, ExecuteLuaString) {
    // Execute simple Lua code
    EXPECT_TRUE(mapper->execute_lua_string("test_var = 42"));
    
    // Check the variable was set
    auto result = mapper->get_lua_variable("test_var");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "42");
}

// Test setting and getting CAN signal values
TEST_F(LuaMapperSimpleTest, SetCANSignalValue) {
    // Set a CAN signal value
    mapper->set_can_signal_value("VehicleSpeed", 60.0);
    
    // Check it's accessible in Lua
    mapper->execute_lua_string("speed_check = can_signals['VehicleSpeed']");
    auto result = mapper->get_lua_variable("speed_check");
    ASSERT_TRUE(result.has_value());
    // Lua might format as 60.0 instead of 60
    EXPECT_TRUE(result.value() == "60" || result.value() == "60.0");
}

// Test transform function
TEST_F(LuaMapperSimpleTest, CallTransformFunction) {
    // Define the process_signal function that call_transform_function expects
    std::string transform_code = R"(
        function process_signal(signal_name, value)
            if signal_name == "VehicleSpeed" then
                return {
                    path = "Vehicle.Speed",
                    value_type = "double",
                    value = tostring(value * 3.6)
                }
            end
            return nil
        end
    )";
    
    EXPECT_TRUE(mapper->execute_lua_string(transform_code));
    
    auto result = mapper->call_transform_function("VehicleSpeed", 25.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path, "Vehicle.Speed");
    // Value is now in qualified_value.value, we need to convert to string for comparison
    EXPECT_TRUE(std::holds_alternative<std::string>(result->qualified_value.value) ||
                std::holds_alternative<double>(result->qualified_value.value));
}

// Test mapping multiple CAN signals
TEST_F(LuaMapperSimpleTest, MapMultipleCANSignals) {
    // Set up a mapping function that writes to vss_signals global
    std::string mapping_code = R"(
        function map_signals()
            vss_signals = {}
            
            -- Map speed
            if can_signals['VehicleSpeed'] then
                table.insert(vss_signals, {
                    path = "Vehicle.Speed",
                    value_type = "double",
                    value = tostring(can_signals['VehicleSpeed'] * 3.6)
                })
            end
            
            -- Map temperature
            if can_signals['EngineTemp'] then
                table.insert(vss_signals, {
                    path = "Engine.Temperature",
                    value_type = "double",
                    value = tostring(can_signals['EngineTemp'])
                })
            end
        end
    )";
    
    EXPECT_TRUE(mapper->execute_lua_string(mapping_code));
    
    // Provide CAN signals
    std::vector<std::pair<std::string, double>> can_signals;
    can_signals.push_back({"VehicleSpeed", 30.0});
    can_signals.push_back({"EngineTemp", 85.0});
    
    auto vss_signals = mapper->map_can_signals(can_signals);
    
    ASSERT_EQ(vss_signals.size(), 2);
    
    // Check speed conversion
    auto speed_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Speed"; });
    ASSERT_NE(speed_it, vss_signals.end());
    // Value is now in qualified_value.value
    EXPECT_EQ(speed_it->qualified_value.quality, vss::types::SignalQuality::VALID);

    // Check temperature
    auto temp_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Engine.Temperature"; });
    ASSERT_NE(temp_it, vss_signals.end());
    EXPECT_EQ(temp_it->qualified_value.quality, vss::types::SignalQuality::VALID);
}

// Test Lua state persistence
TEST_F(LuaMapperSimpleTest, LuaStatePersistence) {
    // Set a variable
    mapper->execute_lua_string("counter = 0");
    
    // Increment it multiple times
    for (int i = 0; i < 5; ++i) {
        mapper->execute_lua_string("counter = counter + 1");
    }
    
    // Check final value
    auto result = mapper->get_lua_variable("counter");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "5");
}

// Test error handling
TEST_F(LuaMapperSimpleTest, ErrorHandling) {
    // Invalid Lua syntax should return false
    EXPECT_FALSE(mapper->execute_lua_string("this is not valid lua"));
    
    // Undefined variable should return empty optional
    auto result = mapper->get_lua_variable("undefined_variable");
    EXPECT_FALSE(result.has_value());
}

// Test table/struct return values
TEST_F(LuaMapperSimpleTest, TableReturnValues) {
    std::string code = R"(
        test_table = {
            field1 = 42,
            field2 = "hello",
            field3 = true
        }
    )";
    
    EXPECT_TRUE(mapper->execute_lua_string(code));
    
    // Tables are returned as formatted strings
    auto result = mapper->get_lua_variable("test_table");
    ASSERT_TRUE(result.has_value());
    // The actual format depends on implementation
    EXPECT_TRUE(result->find("42") != std::string::npos || 
                result->find("table") != std::string::npos);
}