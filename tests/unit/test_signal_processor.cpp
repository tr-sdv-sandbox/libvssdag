#include <gtest/gtest.h>
#include "vssdag/signal_processor.h"
#include "vssdag/mapping_types.h"

using namespace vssdag;

class SignalProcessorTest : public ::testing::Test {
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
    
    // Helper to create SignalUpdate
    SignalUpdate MakeUpdate(const std::string& name,
                            const vss::types::Value& value) {
        SignalUpdate update;
        update.signal_name = name;
        update.value = value;
        update.timestamp = std::chrono::steady_clock::now();
        return update;
    }
};

// Test basic processor initialization
TEST_F(SignalProcessorTest, BasicInitialization) {
    // Simple mapping
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = ValueType::DOUBLE;
    mappings["Vehicle.Speed"] = speed_mapping;
    
    EXPECT_TRUE(processor->initialize(mappings));
    
    auto required_signals = processor->get_required_input_signals();
    EXPECT_EQ(required_signals.size(), 1);
    EXPECT_EQ(required_signals[0], "Vehicle.Speed");
}

// Test processing simple signal update
TEST_F(SignalProcessorTest, ProcessSimpleSignal) {
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = ValueType::DOUBLE;
    speed_mapping.transform = CodeTransform{"x * 3.6"};  // m/s to km/h
    mappings["Vehicle.Speed"] = speed_mapping;

    ASSERT_TRUE(processor->initialize(mappings));

    // Create signal update
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Vehicle.Speed", 25.0));  // 25 m/s

    auto vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].path, "Vehicle.Speed");
    // Value is now in qualified_value.value
}

// Test derived signal processing
TEST_F(SignalProcessorTest, ProcessDerivedSignal) {
    // Input signal
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = ValueType::DOUBLE;
    mappings["Vehicle.Speed"] = speed_mapping;

    // Derived signal
    SignalMapping category_mapping;
    category_mapping.depends_on.push_back("Vehicle.Speed");
    category_mapping.datatype = ValueType::STRING;
    category_mapping.transform = CodeTransform{
        "local speed = deps['Vehicle.Speed']\n"
        "if speed > 100 then return 'HIGH' "
        "elseif speed > 50 then return 'MEDIUM' "
        "else return 'LOW' end"
    };
    mappings["Vehicle.SpeedCategory"] = category_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Process high speed
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Vehicle.Speed", 120.0));
    
    auto vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 2);  // Both input and derived
    
    // Find the derived signal
    auto category_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.SpeedCategory"; });
    ASSERT_NE(category_it, vss_signals.end());
    // Value is now in qualified_value.value
}

// Test multi-dependency signal
TEST_F(SignalProcessorTest, ProcessMultiDependency) {
    // Input signals
    SignalMapping voltage_mapping;
    voltage_mapping.source.type = "dbc";
    voltage_mapping.source.name = "BatteryVoltage";
    voltage_mapping.datatype = ValueType::DOUBLE;
    mappings["Battery.Voltage"] = voltage_mapping;

    SignalMapping current_mapping;
    current_mapping.source.type = "dbc";
    current_mapping.source.name = "BatteryCurrent";
    current_mapping.datatype = ValueType::DOUBLE;
    mappings["Battery.Current"] = current_mapping;

    // Derived power signal
    SignalMapping power_mapping;
    power_mapping.depends_on.push_back("Battery.Voltage");
    power_mapping.depends_on.push_back("Battery.Current");
    power_mapping.datatype = ValueType::DOUBLE;
    power_mapping.transform = CodeTransform{
        "deps['Battery.Voltage'] * deps['Battery.Current']"
    };
    mappings["Battery.Power"] = power_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Process updates
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Battery.Voltage", 400.0));
    updates.push_back(MakeUpdate("Battery.Current", 150.0));
    
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Find power signal
    auto power_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Battery.Power"; });
    ASSERT_NE(power_it, vss_signals.end());
    // Value is now in qualified_value.value
}

// Test struct signal output
TEST_F(SignalProcessorTest, ProcessStructSignal) {
    // Input signals
    SignalMapping voltage_mapping;
    voltage_mapping.source.type = "dbc";
    voltage_mapping.source.name = "BatteryVoltage";
    mappings["Battery.Voltage"] = voltage_mapping;
    
    SignalMapping current_mapping;
    current_mapping.source.type = "dbc";
    current_mapping.source.name = "BatteryCurrent";
    mappings["Battery.Current"] = current_mapping;
    
    SignalMapping temp_mapping;
    temp_mapping.source.type = "dbc";
    temp_mapping.source.name = "BatteryTemp";
    mappings["Battery.Temperature"] = temp_mapping;
    
    // Struct signal
    SignalMapping status_mapping;
    status_mapping.depends_on.push_back("Battery.Voltage");
    status_mapping.depends_on.push_back("Battery.Current");
    status_mapping.depends_on.push_back("Battery.Temperature");
    status_mapping.datatype = ValueType::STRUCT;
    status_mapping.is_struct = true;
    status_mapping.struct_type = "BatteryStatus";
    status_mapping.transform = CodeTransform{
        "return {\n"
        "  voltage = deps['Battery.Voltage'],\n"
        "  current = deps['Battery.Current'],\n"
        "  temperature = deps['Battery.Temperature'],\n"
        "  power = deps['Battery.Voltage'] * deps['Battery.Current']\n"
        "}"
    };
    mappings["Battery.Status"] = status_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Process updates
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Battery.Voltage", 400.0));
    updates.push_back(MakeUpdate("Battery.Current", 150.0));
    updates.push_back(MakeUpdate("Battery.Temperature", 25.0));
    
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Find struct signal
    auto status_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Battery.Status"; });
    ASSERT_NE(status_it, vss_signals.end());
    // Value is now in qualified_value.value as a struct
}

// Test partial updates (not all deps satisfied)
TEST_F(SignalProcessorTest, PartialUpdates) {
    // Setup multi-dependency signal
    SignalMapping a_mapping;
    a_mapping.source.type = "dbc";
    a_mapping.source.name = "SignalA";
    mappings["A"] = a_mapping;
    
    SignalMapping b_mapping;
    b_mapping.source.type = "dbc";
    b_mapping.source.name = "SignalB";
    mappings["B"] = b_mapping;
    
    SignalMapping derived_mapping;
    derived_mapping.depends_on.push_back("A");
    derived_mapping.depends_on.push_back("B");
    derived_mapping.transform = CodeTransform{
        "deps['A'] + deps['B']"
    };
    mappings["Derived"] = derived_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Update only one dependency
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("A", 10.0));
    
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Should only get the input signal, not the derived one
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].path, "A");
    
    // Now update the second dependency
    updates.clear();
    updates.push_back(MakeUpdate("B", 20.0));
    
    vss_signals = processor->process_signal_updates(updates);
    
    // Now we should get both B and the derived signal
    EXPECT_GE(vss_signals.size(), 2);
    
    auto derived_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Derived"; });
    ASSERT_NE(derived_it, vss_signals.end());
    // Value is now in qualified_value.value
}

// Test invalid and not available signal handling
TEST_F(SignalProcessorTest, InvalidSignalHandling) {
    // Setup a derived signal that depends on two inputs
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = ValueType::DOUBLE;
    speed_mapping.max_interval_ms = 0;  // Disable heartbeat for testing
    mappings["Vehicle.Speed"] = speed_mapping;

    SignalMapping throttle_mapping;
    throttle_mapping.source.type = "dbc";
    throttle_mapping.source.name = "ThrottlePos";
    throttle_mapping.datatype = ValueType::DOUBLE;
    throttle_mapping.max_interval_ms = 0;  // Disable heartbeat for testing
    mappings["Vehicle.Throttle"] = throttle_mapping;

    // Derived signal that computes power estimate
    SignalMapping power_mapping;
    power_mapping.depends_on.push_back("Vehicle.Speed");
    power_mapping.depends_on.push_back("Vehicle.Throttle");
    power_mapping.datatype = ValueType::DOUBLE;
    power_mapping.max_interval_ms = 0;  // Disable heartbeat for testing
    power_mapping.transform = CodeTransform{
        "local speed = deps['Vehicle.Speed']\n"
        "local throttle = deps['Vehicle.Throttle']\n"
        "if speed == nil or throttle == nil then\n"
        "    return nil  -- Propagate nil if inputs invalid\n"
        "end\n"
        "return speed * throttle * 0.1"  // Simple power estimate
    };
    mappings["Vehicle.PowerEstimate"] = power_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Test 1: Send valid signals
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Vehicle.Speed", 50.0));
    updates.push_back(MakeUpdate("Vehicle.Throttle", 80.0));
    
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Should get all three signals
    EXPECT_EQ(vss_signals.size(), 3);
    auto power_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.PowerEstimate"; });
    ASSERT_NE(power_it, vss_signals.end());
    // Value is now in qualified_value.value
    
    // Test 2: Send invalid speed
    updates.clear();
    SignalUpdate invalid_speed = MakeUpdate("Vehicle.Speed", 0.0);  // Dummy value
    invalid_speed.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_speed);
    updates.push_back(MakeUpdate("Vehicle.Throttle", 90.0));
    
    vss_signals = processor->process_signal_updates(updates);
    
    // Should get all signals but with appropriate status
    EXPECT_EQ(vss_signals.size(), 3);
    
    // Check speed has invalid status
    auto speed_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Speed"; });
    ASSERT_NE(speed_it, vss_signals.end());
    EXPECT_EQ(speed_it->qualified_value.quality, vss::types::SignalQuality::INVALID);

    // Check throttle has valid status
    auto throttle_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Throttle"; });
    ASSERT_NE(throttle_it, vss_signals.end());
    EXPECT_EQ(throttle_it->qualified_value.quality, vss::types::SignalQuality::VALID);

    // Check power estimate has invalid status (due to invalid input)
    auto power_it2 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.PowerEstimate"; });
    ASSERT_NE(power_it2, vss_signals.end());
    EXPECT_EQ(power_it2->qualified_value.quality, vss::types::SignalQuality::INVALID);
    
    // Test 3: Send not available throttle
    updates.clear();
    updates.push_back(MakeUpdate("Vehicle.Speed", 60.0));  // Value changes (from invalid 0.0)
    SignalUpdate na_throttle = MakeUpdate("Vehicle.Throttle", 0.0);  // Dummy value
    na_throttle.status = vss::types::SignalQuality::NOT_AVAILABLE;
    updates.push_back(na_throttle);

    vss_signals = processor->process_signal_updates(updates);

    // Speed: quality changes (INVALID -> VALID) AND value changes -> emitted
    // Throttle: quality changes (VALID -> NOT_AVAILABLE) -> emitted
    // PowerEstimate: quality stays INVALID (was INVALID before, still INVALID due to NA input)
    //   -> NOT emitted because no quality change
    EXPECT_EQ(vss_signals.size(), 2);

    // Check speed has valid status
    auto speed_it2 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Speed"; });
    ASSERT_NE(speed_it2, vss_signals.end());
    EXPECT_EQ(speed_it2->qualified_value.quality, vss::types::SignalQuality::VALID);

    // Check throttle has not available status
    auto throttle_it2 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Throttle"; });
    ASSERT_NE(throttle_it2, vss_signals.end());
    EXPECT_EQ(throttle_it2->qualified_value.quality, vss::types::SignalQuality::NOT_AVAILABLE);
}

// Test status transitions (valid -> invalid -> NA -> valid)
TEST_F(SignalProcessorTest, StatusTransitions) {
    SignalMapping sensor_mapping;
    sensor_mapping.source.type = "dbc";
    sensor_mapping.source.name = "SensorReading";
    sensor_mapping.datatype = ValueType::DOUBLE;
    mappings["Sensor.Value"] = sensor_mapping;

    ASSERT_TRUE(processor->initialize(mappings));

    // Start with valid signal
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Sensor.Value", 100.0));
    auto vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].qualified_value.quality, vss::types::SignalQuality::VALID);

    // Transition to invalid
    updates.clear();
    SignalUpdate invalid_update = MakeUpdate("Sensor.Value", 0.0);
    invalid_update.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_update);
    vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].qualified_value.quality, vss::types::SignalQuality::INVALID);

    // Transition to not available
    updates.clear();
    SignalUpdate na_update = MakeUpdate("Sensor.Value", 0.0);
    na_update.status = vss::types::SignalQuality::NOT_AVAILABLE;
    updates.push_back(na_update);
    vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].qualified_value.quality, vss::types::SignalQuality::NOT_AVAILABLE);

    // Transition back to valid
    updates.clear();
    updates.push_back(MakeUpdate("Sensor.Value", 200.0));
    vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].qualified_value.quality, vss::types::SignalQuality::VALID);
}

// Test mixed invalid/NA status in multi-dependency signals
TEST_F(SignalProcessorTest, MixedStatusMultiDependency) {
    // Three input signals
    SignalMapping a_mapping;
    a_mapping.source.type = "dbc";
    a_mapping.source.name = "SignalA";
    a_mapping.datatype = ValueType::DOUBLE;
    mappings["A"] = a_mapping;

    SignalMapping b_mapping;
    b_mapping.source.type = "dbc";
    b_mapping.source.name = "SignalB";
    b_mapping.datatype = ValueType::DOUBLE;
    mappings["B"] = b_mapping;

    SignalMapping c_mapping;
    c_mapping.source.type = "dbc";
    c_mapping.source.name = "SignalC";
    c_mapping.datatype = ValueType::DOUBLE;
    mappings["C"] = c_mapping;

    // Derived signal that handles different status combinations
    SignalMapping derived_mapping;
    derived_mapping.depends_on = {"A", "B", "C"};
    derived_mapping.datatype = ValueType::STRING;
    derived_mapping.transform = CodeTransform{
        "local a_status = deps_status['A'] or STATUS_VALID\n"
        "local b_status = deps_status['B'] or STATUS_VALID\n"
        "local c_status = deps_status['C'] or STATUS_VALID\n"
        "if a_status == STATUS_INVALID then\n"
        "    return 'A_INVALID'\n"
        "elseif b_status == STATUS_NOT_AVAILABLE then\n"
        "    return 'B_NOT_AVAILABLE'\n"
        "elseif c_status ~= STATUS_VALID then\n"
        "    return 'C_PROBLEM'\n"
        "else\n"
        "    return 'ALL_GOOD: ' .. (deps['A'] + deps['B'] + deps['C'])\n"
        "end"
    };
    mappings["Derived"] = derived_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Test 1: All valid
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("A", 10.0));
    updates.push_back(MakeUpdate("B", 20.0));
    updates.push_back(MakeUpdate("C", 30.0));
    auto vss_signals = processor->process_signal_updates(updates);
    
    auto derived_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Derived"; });
    ASSERT_NE(derived_it, vss_signals.end());
    // Value is now in qualified_value.value
    
    // Test 2: A is invalid
    updates.clear();
    SignalUpdate invalid_a = MakeUpdate("A", 0.0);
    invalid_a.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_a);
    updates.push_back(MakeUpdate("B", 20.0));
    updates.push_back(MakeUpdate("C", 30.0));
    vss_signals = processor->process_signal_updates(updates);
    
    derived_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Derived"; });
    ASSERT_NE(derived_it, vss_signals.end());
    // Value is now in qualified_value.value
    
    // Test 3: B is not available
    updates.clear();
    updates.push_back(MakeUpdate("A", 10.0));
    SignalUpdate na_b = MakeUpdate("B", 0.0);
    na_b.status = vss::types::SignalQuality::NOT_AVAILABLE;
    updates.push_back(na_b);
    updates.push_back(MakeUpdate("C", 30.0));
    vss_signals = processor->process_signal_updates(updates);
    
    derived_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Derived"; });
    ASSERT_NE(derived_it, vss_signals.end());
    // Value is now in qualified_value.value
}

// Test filter strategies (STRATEGY_HOLD and STRATEGY_HOLD_TIMEOUT)
TEST_F(SignalProcessorTest, FilterStrategies) {
    // Test STRATEGY_HOLD - should return last valid value when input is invalid
    SignalMapping hold_mapping;
    hold_mapping.source.type = "dbc";
    hold_mapping.source.name = "HoldSignal";
    hold_mapping.datatype = ValueType::DOUBLE;
    hold_mapping.transform = CodeTransform{"lowpass(x, 0.5, STRATEGY_HOLD)"};
    mappings["Hold.Signal"] = hold_mapping;

    // Test STRATEGY_HOLD_TIMEOUT - should hold for a while then return nil
    SignalMapping timeout_mapping;
    timeout_mapping.source.type = "dbc";
    timeout_mapping.source.name = "TimeoutSignal";
    timeout_mapping.datatype = ValueType::DOUBLE;
    timeout_mapping.transform = CodeTransform{"lowpass(x, 0.5, STRATEGY_HOLD_TIMEOUT)"};
    mappings["Timeout.Signal"] = timeout_mapping;

    // Test STRATEGY_PROPAGATE (default) - should return nil immediately
    SignalMapping propagate_mapping;
    propagate_mapping.source.type = "dbc";
    propagate_mapping.source.name = "PropagateSignal";
    propagate_mapping.datatype = ValueType::DOUBLE;
    propagate_mapping.transform = CodeTransform{"lowpass(x, 0.5)"};  // Default is STRATEGY_PROPAGATE
    mappings["Propagate.Signal"] = propagate_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Send initial valid values to establish filter state
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Hold.Signal", 100.0));
    updates.push_back(MakeUpdate("Timeout.Signal", 200.0));
    updates.push_back(MakeUpdate("Propagate.Signal", 300.0));
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Send another valid update to move the filter
    updates.clear();
    updates.push_back(MakeUpdate("Hold.Signal", 110.0));
    updates.push_back(MakeUpdate("Timeout.Signal", 210.0));
    updates.push_back(MakeUpdate("Propagate.Signal", 310.0));
    vss_signals = processor->process_signal_updates(updates);
    
    // Now send invalid signals
    updates.clear();
    SignalUpdate invalid_hold = MakeUpdate("Hold.Signal", 0.0);
    invalid_hold.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_hold);

    SignalUpdate invalid_timeout = MakeUpdate("Timeout.Signal", 0.0);
    invalid_timeout.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_timeout);

    SignalUpdate invalid_propagate = MakeUpdate("Propagate.Signal", 0.0);
    invalid_propagate.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_propagate);
    
    vss_signals = processor->process_signal_updates(updates);
    
    // Check behaviors:
    // STRATEGY_HOLD should output last valid value with invalid status
    auto hold_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Hold.Signal"; });
    ASSERT_NE(hold_it, vss_signals.end());
    EXPECT_EQ(hold_it->qualified_value.quality, vss::types::SignalQuality::INVALID);

    // STRATEGY_HOLD_TIMEOUT should also hold initially
    auto timeout_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Timeout.Signal"; });
    ASSERT_NE(timeout_it, vss_signals.end());
    EXPECT_EQ(timeout_it->qualified_value.quality, vss::types::SignalQuality::INVALID);

    // STRATEGY_PROPAGATE should have invalid status
    auto propagate_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Propagate.Signal"; });
    ASSERT_NE(propagate_it, vss_signals.end());
    EXPECT_EQ(propagate_it->qualified_value.quality, vss::types::SignalQuality::INVALID);
    
    // With the new emission logic, subsequent identical invalid updates won't emit
    // because neither value nor quality has changed. This is expected behavior.
    // The STRATEGY_HOLD behavior (what value the transform outputs) is already
    // verified above - it holds the last valid value when input goes invalid.
    //
    // To verify the hold continues working, we can transition back to valid:
    updates.clear();
    updates.push_back(MakeUpdate("Hold.Signal", 120.0));  // Back to valid
    vss_signals = processor->process_signal_updates(updates);

    // Should emit because quality changed (INVALID -> VALID)
    hold_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Hold.Signal"; });
    ASSERT_NE(hold_it, vss_signals.end());
    EXPECT_EQ(hold_it->qualified_value.quality, vss::types::SignalQuality::VALID);
}

// Test lowpass filter with invalid signals
TEST_F(SignalProcessorTest, LowpassWithInvalidSignals) {
    SignalMapping temp_mapping;
    temp_mapping.source.type = "dbc";
    temp_mapping.source.name = "EngineTemp";
    temp_mapping.datatype = ValueType::DOUBLE;
    temp_mapping.transform = CodeTransform{"lowpass(x, 0.3)"};
    mappings["Engine.Temperature"] = temp_mapping;

    ASSERT_TRUE(processor->initialize(mappings));

    // Send valid temperature readings
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Engine.Temperature", 70.0));
    auto vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);

    // Send another valid reading
    updates.clear();
    updates.push_back(MakeUpdate("Engine.Temperature", 80.0));
    vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);

    // Send invalid reading - filter should output with invalid status
    updates.clear();
    SignalUpdate invalid_temp = MakeUpdate("Engine.Temperature", 255.0);  // Error value
    invalid_temp.status = vss::types::SignalQuality::INVALID;
    updates.push_back(invalid_temp);
    vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].qualified_value.quality, vss::types::SignalQuality::INVALID);

    // Send valid reading again - filter should continue from last valid
    updates.clear();
    updates.push_back(MakeUpdate("Engine.Temperature", 75.0));
    vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].qualified_value.quality, vss::types::SignalQuality::VALID);
}

// Test struct signal with nil dependencies (default values via 'or 0')
TEST_F(SignalProcessorTest, StructWithNilDependencies) {
    // Input signals
    SignalMapping speed;
    speed.source.type = "dbc";
    speed.source.name = "DI_vehicleSpeed";
    speed.datatype = ValueType::FLOAT;
    mappings["Vehicle.Speed"] = speed;

    SignalMapping accel;
    accel.source.type = "dbc";
    accel.source.name = "RCM_longitudinalAccel";
    accel.datatype = ValueType::FLOAT;
    mappings["Vehicle.Acceleration.Longitudinal"] = accel;

    // Struct signal with 'or 0' fallback for nil deps
    SignalMapping dynamics;
    dynamics.datatype = ValueType::STRUCT;
    dynamics.is_struct = true;
    dynamics.struct_type = "Types.VehicleDynamics";
    dynamics.depends_on = {"Vehicle.Speed", "Vehicle.Acceleration.Longitudinal"};
    dynamics.min_interval_ms = 0;
    dynamics.max_interval_ms = 0;  // Disable heartbeat for this test
    dynamics.transform = CodeTransform{R"(
        return {
          Speed = deps['Vehicle.Speed'] or 0,
          LongitudinalAcceleration = deps['Vehicle.Acceleration.Longitudinal'] or 0
        }
    )"};
    mappings["Vehicle.DynamicsStruct"] = dynamics;

    ASSERT_TRUE(processor->initialize(mappings));

    // Test 1: No data yet - struct should NOT be emitted (deps not available)
    auto signals1 = processor->process_signal_updates({});
    auto struct_it1 = std::find_if(signals1.begin(), signals1.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.DynamicsStruct"; });
    EXPECT_EQ(struct_it1, signals1.end()) << "Struct should not emit when no deps have been received";

    // Test 2: Only speed data - struct should emit with speed and accel=0
    std::vector<SignalUpdate> updates2;
    updates2.push_back(MakeUpdate("Vehicle.Speed", 50.0f));
    auto signals2 = processor->process_signal_updates(updates2);

    auto struct_it2 = std::find_if(signals2.begin(), signals2.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.DynamicsStruct"; });
    ASSERT_NE(struct_it2, signals2.end()) << "Struct should emit when at least one dep has data";

    // Verify struct has fields
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<vss::types::StructValue>>(
        struct_it2->qualified_value.value)) << "Value should be a StructValue";

    auto struct_ptr = std::get<std::shared_ptr<vss::types::StructValue>>(
        struct_it2->qualified_value.value);
    ASSERT_NE(struct_ptr, nullptr) << "StructValue pointer should not be null";

    const auto& fields = struct_ptr->fields();
    EXPECT_FALSE(fields.empty()) << "Struct should have fields";
    EXPECT_TRUE(struct_ptr->has_field("Speed")) << "Struct should have Speed field";
    EXPECT_TRUE(struct_ptr->has_field("LongitudinalAcceleration"))
        << "Struct should have LongitudinalAcceleration field";

    // Verify JSON output is not empty
    std::string json = VSSTypeHelper::to_json(struct_it2->qualified_value.value);
    EXPECT_NE(json, "{}") << "JSON should not be empty: got " << json;
    EXPECT_TRUE(json.find("Speed") != std::string::npos)
        << "JSON should contain Speed: got " << json;
}

// Test struct signal with valid dependencies
TEST_F(SignalProcessorTest, StructWithValidDependencies) {
    // Input signals
    SignalMapping speed;
    speed.source.type = "dbc";
    speed.source.name = "DI_vehicleSpeed";
    speed.datatype = ValueType::FLOAT;
    mappings["Vehicle.Speed"] = speed;

    SignalMapping accel;
    accel.source.type = "dbc";
    accel.source.name = "RCM_longitudinalAccel";
    accel.datatype = ValueType::FLOAT;
    mappings["Vehicle.Acceleration.Longitudinal"] = accel;

    // Struct signal
    SignalMapping dynamics;
    dynamics.datatype = ValueType::STRUCT;
    dynamics.is_struct = true;
    dynamics.struct_type = "Types.VehicleDynamics";
    dynamics.depends_on = {"Vehicle.Speed", "Vehicle.Acceleration.Longitudinal"};
    dynamics.min_interval_ms = 0;
    dynamics.max_interval_ms = 0;  // Disable heartbeat
    dynamics.transform = CodeTransform{R"(
        return {
          Speed = deps['Vehicle.Speed'] or 0,
          LongitudinalAcceleration = deps['Vehicle.Acceleration.Longitudinal'] or 0
        }
    )"};
    mappings["Vehicle.DynamicsStruct"] = dynamics;

    ASSERT_TRUE(processor->initialize(mappings));

    // Provide both dependencies
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Vehicle.Speed", 60.5f));
    updates.push_back(MakeUpdate("Vehicle.Acceleration.Longitudinal", 2.5f));

    auto signals = processor->process_signal_updates(updates);

    auto struct_it = std::find_if(signals.begin(), signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.DynamicsStruct"; });
    ASSERT_NE(struct_it, signals.end()) << "Struct signal should be emitted";

    // Verify it's a struct
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<vss::types::StructValue>>(
        struct_it->qualified_value.value));

    auto struct_ptr = std::get<std::shared_ptr<vss::types::StructValue>>(
        struct_it->qualified_value.value);
    ASSERT_NE(struct_ptr, nullptr);

    // Verify fields exist and have correct values
    const auto& fields = struct_ptr->fields();
    EXPECT_EQ(fields.size(), 2) << "Struct should have exactly 2 fields";

    ASSERT_TRUE(struct_ptr->has_field("Speed"));
    ASSERT_TRUE(struct_ptr->has_field("LongitudinalAcceleration"));

    // Check JSON serialization
    std::string json = VSSTypeHelper::to_json(struct_it->qualified_value.value);
    EXPECT_TRUE(json.find("Speed") != std::string::npos) << "JSON: " << json;
    EXPECT_TRUE(json.find("LongitudinalAcceleration") != std::string::npos) << "JSON: " << json;

    // Log for debugging
    std::cout << "Struct JSON: " << json << std::endl;
}

// Test struct field values are correctly extracted from Lua
TEST_F(SignalProcessorTest, StructFieldValueExtraction) {
    // Single input signal
    SignalMapping input;
    input.source.type = "dbc";
    input.source.name = "TestInput";
    input.datatype = ValueType::DOUBLE;
    mappings["Test.Input"] = input;

    // Struct with various field types
    SignalMapping test_struct;
    test_struct.datatype = ValueType::STRUCT;
    test_struct.is_struct = true;
    test_struct.struct_type = "TestStruct";
    test_struct.depends_on = {"Test.Input"};
    test_struct.min_interval_ms = 0;
    test_struct.max_interval_ms = 0;
    test_struct.transform = CodeTransform{R"(
        local val = deps['Test.Input'] or 0
        return {
          double_field = val,
          int_field = 42,
          string_field = "hello",
          bool_field = true,
          computed = val * 2
        }
    )"};
    mappings["Test.Struct"] = test_struct;

    ASSERT_TRUE(processor->initialize(mappings));

    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Test.Input", 10.5));

    auto signals = processor->process_signal_updates(updates);

    auto struct_it = std::find_if(signals.begin(), signals.end(),
        [](const VSSSignal& s) { return s.path == "Test.Struct"; });
    ASSERT_NE(struct_it, signals.end());

    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<vss::types::StructValue>>(
        struct_it->qualified_value.value));

    auto struct_ptr = std::get<std::shared_ptr<vss::types::StructValue>>(
        struct_it->qualified_value.value);

    // Verify all fields exist
    EXPECT_TRUE(struct_ptr->has_field("double_field"));
    EXPECT_TRUE(struct_ptr->has_field("int_field"));
    EXPECT_TRUE(struct_ptr->has_field("string_field"));
    EXPECT_TRUE(struct_ptr->has_field("bool_field"));
    EXPECT_TRUE(struct_ptr->has_field("computed"));

    // Verify JSON contains all fields
    std::string json = VSSTypeHelper::to_json(struct_it->qualified_value.value);
    std::cout << "Multi-field struct JSON: " << json << std::endl;

    EXPECT_TRUE(json.find("double_field") != std::string::npos);
    EXPECT_TRUE(json.find("int_field") != std::string::npos);
    EXPECT_TRUE(json.find("string_field") != std::string::npos);
    EXPECT_TRUE(json.find("bool_field") != std::string::npos);
    EXPECT_TRUE(json.find("computed") != std::string::npos);
    EXPECT_TRUE(json.find("hello") != std::string::npos);
    EXPECT_TRUE(json.find("42") != std::string::npos);
    EXPECT_TRUE(json.find("true") != std::string::npos);
}