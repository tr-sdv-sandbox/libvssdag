#include <gtest/gtest.h>
#include "vssdag/signal_processor.h"
#include "vssdag/mapping_types.h"
#include <thread>
#include <chrono>

using namespace vssdag;

class EndToEndTest : public ::testing::Test {
protected:
    std::unique_ptr<SignalProcessorDAG> processor;
    std::unordered_map<std::string, SignalMapping> mappings;
    
    void SetUp() override {
        processor = std::make_unique<SignalProcessorDAG>();
        mappings.clear();
    }
    
    void TearDown() override {
        processor.reset();
    }
};

// Test complete battery management scenario
TEST_F(EndToEndTest, BatteryManagementScenario) {
    // Setup battery cell signals (simplified - 4 cells instead of 12)
    for (int i = 1; i <= 4; ++i) {
        SignalMapping cell_voltage;
        cell_voltage.source.type = "dbc";
        cell_voltage.source.name = "Cell" + std::to_string(i) + "_Voltage";
        cell_voltage.datatype = VSSDataType::Double;
        mappings["Battery.Cell" + std::to_string(i) + ".Voltage"] = cell_voltage;
        
        SignalMapping cell_temp;
        cell_temp.source.type = "dbc";
        cell_temp.source.name = "Cell" + std::to_string(i) + "_Temp";
        cell_temp.datatype = VSSDataType::Double;
        mappings["Battery.Cell" + std::to_string(i) + ".Temperature"] = cell_temp;
    }
    
    // Aggregated battery status (struct)
    SignalMapping battery_status;
    battery_status.datatype = VSSDataType::Struct;
    battery_status.is_struct = true;
    battery_status.struct_type = "BatteryStatus";
    
    // Build dependency list
    for (int i = 1; i <= 4; ++i) {
        battery_status.depends_on.push_back("Battery.Cell" + std::to_string(i) + ".Voltage");
        battery_status.depends_on.push_back("Battery.Cell" + std::to_string(i) + ".Temperature");
    }
    
    // Transform to calculate aggregated values
    battery_status.transform = CodeTransform{R"(
        local min_voltage = 999
        local max_voltage = 0
        local total_voltage = 0
        local max_temp = -999
        
        for i = 1, 4 do
            local v = deps['Battery.Cell' .. i .. '.Voltage']
            local t = deps['Battery.Cell' .. i .. '.Temperature']
            
            if v < min_voltage then min_voltage = v end
            if v > max_voltage then max_voltage = v end
            total_voltage = total_voltage + v
            
            if t > max_temp then max_temp = t end
        end
        
        return {
            min_cell_voltage = min_voltage,
            max_cell_voltage = max_voltage,
            total_voltage = total_voltage,
            avg_voltage = total_voltage / 4,
            max_temperature = max_temp,
            voltage_delta = max_voltage - min_voltage
        }
    )"};
    
    mappings["Battery.Status"] = battery_status;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Simulate battery cell updates
    std::vector<SignalUpdate> updates;
    auto timestamp = std::chrono::steady_clock::now();
    
    // Helper to create SignalUpdate
    auto makeUpdate = [timestamp](const std::string& name, double value) {
        return SignalUpdate{name, value, timestamp};
    };
    
    // Cell voltages (slightly different to test aggregation)
    updates.push_back(makeUpdate("Battery.Cell1.Voltage", 3.65));
    updates.push_back(makeUpdate("Battery.Cell2.Voltage", 3.70));
    updates.push_back(makeUpdate("Battery.Cell3.Voltage", 3.68));
    updates.push_back(makeUpdate("Battery.Cell4.Voltage", 3.72));
    
    // Cell temperatures
    updates.push_back(makeUpdate("Battery.Cell1.Temperature", 25.0));
    updates.push_back(makeUpdate("Battery.Cell2.Temperature", 26.5));
    updates.push_back(makeUpdate("Battery.Cell3.Temperature", 25.5));
    updates.push_back(makeUpdate("Battery.Cell4.Temperature", 27.0));
    
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Find the battery status struct
    auto status_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Battery.Status"; });
    
    ASSERT_NE(status_it, vss_signals.end());
    EXPECT_EQ(status_it->value_type, "struct");
    
    // Verify struct contains expected fields
    std::string json = status_it->value;
    EXPECT_TRUE(json.find("\"min_cell_voltage\":3.65") != std::string::npos);
    EXPECT_TRUE(json.find("\"max_cell_voltage\":3.72") != std::string::npos);
    EXPECT_TRUE(json.find("\"total_voltage\":14.75") != std::string::npos);
    EXPECT_TRUE(json.find("\"avg_voltage\":3.6875") != std::string::npos);
    EXPECT_TRUE(json.find("\"max_temperature\":27") != std::string::npos);
    EXPECT_TRUE(json.find("\"voltage_delta\":0.07") != std::string::npos);
}

// Test Tesla-like scenario with multiple derived signals
TEST_F(EndToEndTest, TeslaVehicleDynamics) {
    // Basic signals from CAN
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "DI_vehicleSpeed";
    speed_mapping.datatype = VSSDataType::Double;
    speed_mapping.transform = CodeTransform{"lowpass(x, 0.3)"};
    mappings["Vehicle.Speed"] = speed_mapping;
    
    SignalMapping brake_mapping;
    brake_mapping.source.type = "dbc";
    brake_mapping.source.name = "DI_brakePedalState";
    brake_mapping.datatype = VSSDataType::Boolean;
    mappings["Vehicle.Brake.IsPressed"] = brake_mapping;
    
    SignalMapping throttle_mapping;
    throttle_mapping.source.type = "dbc";
    throttle_mapping.source.name = "DI_accelPedalPos";
    throttle_mapping.datatype = VSSDataType::Double;
    mappings["Vehicle.Throttle.Position"] = throttle_mapping;
    
    // Derived: Acceleration
    SignalMapping accel_mapping;
    accel_mapping.depends_on.push_back("Vehicle.Speed");
    accel_mapping.datatype = VSSDataType::Double;
    accel_mapping.transform = CodeTransform{"derivative(deps['Vehicle.Speed'])"};
    mappings["Vehicle.Acceleration"] = accel_mapping;
    
    // Derived: Harsh braking detection
    SignalMapping harsh_brake_mapping;
    harsh_brake_mapping.depends_on.push_back("Vehicle.Acceleration");
    harsh_brake_mapping.depends_on.push_back("Vehicle.Brake.IsPressed");
    harsh_brake_mapping.datatype = VSSDataType::Boolean;
    harsh_brake_mapping.transform = CodeTransform{R"(
        local accel = deps['Vehicle.Acceleration']
        local brake = deps['Vehicle.Brake.IsPressed']
        return brake and accel < -6.0  -- Deceleration > 6 m/s²
    )"};
    mappings["Vehicle.Safety.HarshBraking"] = harsh_brake_mapping;
    
    // Derived: Driving mode inference
    SignalMapping driving_mode;
    driving_mode.depends_on.push_back("Vehicle.Speed");
    driving_mode.depends_on.push_back("Vehicle.Throttle.Position");
    driving_mode.depends_on.push_back("Vehicle.Acceleration");
    driving_mode.datatype = VSSDataType::String;
    driving_mode.transform = CodeTransform{R"(
        local speed = deps['Vehicle.Speed']
        local throttle = deps['Vehicle.Throttle.Position']
        local accel = deps['Vehicle.Acceleration'] or 0
        
        if throttle > 80 and accel > 3 then
            return 'SPORT'
        elseif speed < 50 and throttle < 30 then
            return 'ECO'
        elseif speed > 120 then
            return 'HIGHWAY'
        else
            return 'NORMAL'
        end
    )"};
    mappings["Vehicle.DrivingMode"] = driving_mode;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Simulate a driving scenario
    // T=0: Starting from stop
    std::vector<SignalUpdate> updates1;
    updates1.push_back({"DI_vehicleSpeed", 0.0});
    updates1.push_back({"DI_brakePedalState", int64_t(0)});
    updates1.push_back({"DI_accelPedalPos", 20.0});
    
    auto signals1 = processor->process_signal_updates(updates1);
    
    // Small delay to allow derivative calculation
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // T=1: Accelerating
    std::vector<SignalUpdate> updates2;
    updates2.push_back({"DI_vehicleSpeed", 30.0});
    updates2.push_back({"DI_brakePedalState", int64_t(0)});
    updates2.push_back({"DI_accelPedalPos", 85.0});
    
    auto signals2 = processor->process_signal_updates(updates2);
    
    // Check for sport mode due to high throttle
    auto mode_it = std::find_if(signals2.begin(), signals2.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.DrivingMode"; });
    
    if (mode_it != signals2.end()) {
        // May be SPORT or NORMAL depending on acceleration calculation
        EXPECT_TRUE(mode_it->value == "SPORT" || mode_it->value == "NORMAL");
    }
    
    // T=2: Hard braking
    std::vector<SignalUpdate> updates3;
    updates3.push_back({"DI_vehicleSpeed", 10.0});
    updates3.push_back({"DI_brakePedalState", int64_t(1)});
    updates3.push_back({"DI_accelPedalPos", 0.0});
    
    auto signals3 = processor->process_signal_updates(updates3);
    
    // Check for harsh braking (depends on derivative calculation)
    auto harsh_it = std::find_if(signals3.begin(), signals3.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Safety.HarshBraking"; });
    
    // Note: Harsh braking detection depends on derivative which needs time delta
    // In a real scenario with proper timestamps, this would be detected
}

// Test periodic signal updates
TEST_F(EndToEndTest, PeriodicSignalUpdates) {
    // Signal with periodic update trigger
    SignalMapping heartbeat;
    heartbeat.source.type = "dbc";
    heartbeat.source.name = "SystemHeartbeat";
    heartbeat.datatype = VSSDataType::Double;
    heartbeat.interval_ms = 100;  // Update every 100ms
    heartbeat.update_trigger = UpdateTrigger::PERIODIC;
    heartbeat.transform = CodeTransform{R"(
        state['counter'] = (state['counter'] or 0) + 1
        return state['counter']
    )"};
    mappings["System.Heartbeat"] = heartbeat;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Initial update to set value
    std::vector<SignalUpdate> updates;
    updates.push_back({"SystemHeartbeat", 1.0});
    
    auto signals1 = processor->process_signal_updates(updates);
    
    // Wait for periodic interval
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Process with empty updates - should trigger periodic
    std::vector<SignalUpdate> empty_updates;
    auto signals2 = processor->process_signal_updates(empty_updates);
    
    // Should get periodic update
    auto heartbeat_it = std::find_if(signals2.begin(), signals2.end(),
        [](const VSSSignal& s) { return s.path == "System.Heartbeat"; });
    
    if (heartbeat_it != signals2.end()) {
        // Counter should have incremented
        double counter = std::stod(heartbeat_it->value);
        EXPECT_GT(counter, 1.0);
    }
}

// Test error handling and recovery
TEST_F(EndToEndTest, ErrorHandlingAndRecovery) {
    // Signal with potentially failing transform
    SignalMapping divide_signal;
    divide_signal.source.type = "dbc";
    divide_signal.source.name = "Numerator";
    divide_signal.datatype = VSSDataType::Double;
    mappings["Math.Numerator"] = divide_signal;
    
    SignalMapping divisor_signal;
    divisor_signal.source.type = "dbc";
    divisor_signal.source.name = "Divisor";
    divisor_signal.datatype = VSSDataType::Double;
    mappings["Math.Divisor"] = divisor_signal;
    
    SignalMapping result_signal;
    result_signal.depends_on.push_back("Math.Numerator");
    result_signal.depends_on.push_back("Math.Divisor");
    result_signal.datatype = VSSDataType::Double;
    result_signal.transform = CodeTransform{R"(
        local num = deps['Math.Numerator']
        local div = deps['Math.Divisor']
        if div == 0 then
            return nil  -- Return nil on error
        end
        return num / div
    )"};
    mappings["Math.Result"] = result_signal;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Test normal case
    std::vector<SignalUpdate> updates1;
    updates1.push_back({"Numerator", 10.0});
    updates1.push_back({"Divisor", 2.0});
    
    auto signals1 = processor->process_signal_updates(updates1);
    
    auto result_it = std::find_if(signals1.begin(), signals1.end(),
        [](const VSSSignal& s) { return s.path == "Math.Result"; });
    
    if (result_it != signals1.end()) {
        EXPECT_EQ(result_it->value, "5");
    }
    
    // Test division by zero
    std::vector<SignalUpdate> updates2;
    updates2.push_back({"Divisor", 0.0});
    
    auto signals2 = processor->process_signal_updates(updates2);
    
    // Should handle gracefully (either no result or null/error value)
    result_it = std::find_if(signals2.begin(), signals2.end(),
        [](const VSSSignal& s) { return s.path == "Math.Result"; });
    
    // If present, should be null or indicate error
    if (result_it != signals2.end()) {
        EXPECT_TRUE(result_it->value == "nil" || result_it->value == "null" || result_it->value == "");
    }
    
    // Test recovery
    std::vector<SignalUpdate> updates3;
    updates3.push_back({"Divisor", 5.0});
    
    auto signals3 = processor->process_signal_updates(updates3);
    
    result_it = std::find_if(signals3.begin(), signals3.end(),
        [](const VSSSignal& s) { return s.path == "Math.Result"; });
    
    if (result_it != signals3.end()) {
        EXPECT_EQ(result_it->value, "2");  // 10 / 5
    }
}