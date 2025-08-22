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
                            const std::variant<int64_t, double, std::string>& value) {
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
    speed_mapping.datatype = VSSDataType::Double;
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
    speed_mapping.datatype = VSSDataType::Double;
    speed_mapping.transform = CodeTransform{"x * 3.6"};  // m/s to km/h
    mappings["Vehicle.Speed"] = speed_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Create signal update
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Vehicle.Speed", 25.0));  // 25 m/s
    
    auto vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].path, "Vehicle.Speed");
    EXPECT_EQ(vss_signals[0].value, "90");  // 25 * 3.6 = 90 km/h
}

// Test derived signal processing
TEST_F(SignalProcessorTest, ProcessDerivedSignal) {
    // Input signal
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = VSSDataType::Double;
    mappings["Vehicle.Speed"] = speed_mapping;
    
    // Derived signal
    SignalMapping category_mapping;
    category_mapping.depends_on.push_back("Vehicle.Speed");
    category_mapping.datatype = VSSDataType::String;
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
    EXPECT_EQ(category_it->value, "HIGH");
}

// Test multi-dependency signal
TEST_F(SignalProcessorTest, ProcessMultiDependency) {
    // Input signals
    SignalMapping voltage_mapping;
    voltage_mapping.source.type = "dbc";
    voltage_mapping.source.name = "BatteryVoltage";
    voltage_mapping.datatype = VSSDataType::Double;
    mappings["Battery.Voltage"] = voltage_mapping;
    
    SignalMapping current_mapping;
    current_mapping.source.type = "dbc";
    current_mapping.source.name = "BatteryCurrent";
    current_mapping.datatype = VSSDataType::Double;
    mappings["Battery.Current"] = current_mapping;
    
    // Derived power signal
    SignalMapping power_mapping;
    power_mapping.depends_on.push_back("Battery.Voltage");
    power_mapping.depends_on.push_back("Battery.Current");
    power_mapping.datatype = VSSDataType::Double;
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
    EXPECT_EQ(power_it->value, "60000");  // 400 * 150 = 60000
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
    status_mapping.datatype = VSSDataType::Struct;
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
    EXPECT_EQ(status_it->value_type, "struct");
    
    // Check that the value is valid JSON with expected fields
    std::string json = status_it->value;
    EXPECT_TRUE(json.find("\"voltage\":400") != std::string::npos);
    EXPECT_TRUE(json.find("\"current\":150") != std::string::npos);
    EXPECT_TRUE(json.find("\"temperature\":25") != std::string::npos);
    EXPECT_TRUE(json.find("\"power\":60000") != std::string::npos);
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
    EXPECT_EQ(derived_it->value, "30");  // 10 + 20
}

// Test invalid and not available signal handling
TEST_F(SignalProcessorTest, InvalidSignalHandling) {
    // Setup a derived signal that depends on two inputs
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = VSSDataType::Double;
    mappings["Vehicle.Speed"] = speed_mapping;
    
    SignalMapping throttle_mapping;
    throttle_mapping.source.type = "dbc";
    throttle_mapping.source.name = "ThrottlePos";
    throttle_mapping.datatype = VSSDataType::Double;
    mappings["Vehicle.Throttle"] = throttle_mapping;
    
    // Derived signal that computes power estimate
    SignalMapping power_mapping;
    power_mapping.depends_on.push_back("Vehicle.Speed");
    power_mapping.depends_on.push_back("Vehicle.Throttle");
    power_mapping.datatype = VSSDataType::Double;
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
    EXPECT_EQ(power_it->value, "400");  // 50 * 80 * 0.1
    
    // Test 2: Send invalid speed
    updates.clear();
    SignalUpdate invalid_speed = MakeUpdate("Vehicle.Speed", 0.0);  // Dummy value
    invalid_speed.status = SignalStatus::Invalid;
    updates.push_back(invalid_speed);
    updates.push_back(MakeUpdate("Vehicle.Throttle", 90.0));
    
    vss_signals = processor->process_signal_updates(updates);
    
    // Should get all signals but with appropriate status
    EXPECT_EQ(vss_signals.size(), 3);
    
    // Check speed has invalid status
    auto speed_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Speed"; });
    ASSERT_NE(speed_it, vss_signals.end());
    EXPECT_EQ(speed_it->status, SignalStatus::Invalid);
    
    // Check throttle has valid status
    auto throttle_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Throttle"; });
    ASSERT_NE(throttle_it, vss_signals.end());
    EXPECT_EQ(throttle_it->status, SignalStatus::Valid);
    
    // Check power estimate has invalid status (due to invalid input)
    auto power_it2 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.PowerEstimate"; });
    ASSERT_NE(power_it2, vss_signals.end());
    EXPECT_EQ(power_it2->status, SignalStatus::Invalid);
    
    // Test 3: Send not available throttle
    updates.clear();
    updates.push_back(MakeUpdate("Vehicle.Speed", 60.0));
    SignalUpdate na_throttle = MakeUpdate("Vehicle.Throttle", 0.0);  // Dummy value
    na_throttle.status = SignalStatus::NotAvailable;
    updates.push_back(na_throttle);
    
    vss_signals = processor->process_signal_updates(updates);
    
    // Should get all signals but with appropriate status
    EXPECT_EQ(vss_signals.size(), 3);
    
    // Check speed has valid status
    auto speed_it2 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Speed"; });
    ASSERT_NE(speed_it2, vss_signals.end());
    EXPECT_EQ(speed_it2->status, SignalStatus::Valid);
    
    // Check throttle has not available status
    auto throttle_it2 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.Throttle"; });
    ASSERT_NE(throttle_it2, vss_signals.end());
    EXPECT_EQ(throttle_it2->status, SignalStatus::NotAvailable);
    
    // Check power estimate has invalid status (due to NA input)
    auto power_it3 = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Vehicle.PowerEstimate"; });
    ASSERT_NE(power_it3, vss_signals.end());
    EXPECT_EQ(power_it3->status, SignalStatus::Invalid);
}

// Test status transitions (valid -> invalid -> NA -> valid)
TEST_F(SignalProcessorTest, StatusTransitions) {
    SignalMapping sensor_mapping;
    sensor_mapping.source.type = "dbc";
    sensor_mapping.source.name = "SensorReading";
    sensor_mapping.datatype = VSSDataType::Double;
    mappings["Sensor.Value"] = sensor_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Start with valid signal
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Sensor.Value", 100.0));
    auto vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].status, SignalStatus::Valid);
    EXPECT_EQ(vss_signals[0].value, "100");
    
    // Transition to invalid
    updates.clear();
    SignalUpdate invalid_update = MakeUpdate("Sensor.Value", 0.0);
    invalid_update.status = SignalStatus::Invalid;
    updates.push_back(invalid_update);
    vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].status, SignalStatus::Invalid);
    
    // Transition to not available
    updates.clear();
    SignalUpdate na_update = MakeUpdate("Sensor.Value", 0.0);
    na_update.status = SignalStatus::NotAvailable;
    updates.push_back(na_update);
    vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].status, SignalStatus::NotAvailable);
    
    // Transition back to valid
    updates.clear();
    updates.push_back(MakeUpdate("Sensor.Value", 200.0));
    vss_signals = processor->process_signal_updates(updates);
    ASSERT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].status, SignalStatus::Valid);
    EXPECT_EQ(vss_signals[0].value, "200");
}

// Test mixed invalid/NA status in multi-dependency signals
TEST_F(SignalProcessorTest, MixedStatusMultiDependency) {
    // Three input signals
    SignalMapping a_mapping;
    a_mapping.source.type = "dbc";
    a_mapping.source.name = "SignalA";
    a_mapping.datatype = VSSDataType::Double;
    mappings["A"] = a_mapping;
    
    SignalMapping b_mapping;
    b_mapping.source.type = "dbc";
    b_mapping.source.name = "SignalB";
    b_mapping.datatype = VSSDataType::Double;
    mappings["B"] = b_mapping;
    
    SignalMapping c_mapping;
    c_mapping.source.type = "dbc";
    c_mapping.source.name = "SignalC";
    c_mapping.datatype = VSSDataType::Double;
    mappings["C"] = c_mapping;
    
    // Derived signal that handles different status combinations
    SignalMapping derived_mapping;
    derived_mapping.depends_on = {"A", "B", "C"};
    derived_mapping.datatype = VSSDataType::String;
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
    EXPECT_EQ(derived_it->value, "ALL_GOOD: 60");
    
    // Test 2: A is invalid
    updates.clear();
    SignalUpdate invalid_a = MakeUpdate("A", 0.0);
    invalid_a.status = SignalStatus::Invalid;
    updates.push_back(invalid_a);
    updates.push_back(MakeUpdate("B", 20.0));
    updates.push_back(MakeUpdate("C", 30.0));
    vss_signals = processor->process_signal_updates(updates);
    
    derived_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Derived"; });
    ASSERT_NE(derived_it, vss_signals.end());
    EXPECT_EQ(derived_it->value, "A_INVALID");
    
    // Test 3: B is not available
    updates.clear();
    updates.push_back(MakeUpdate("A", 10.0));
    SignalUpdate na_b = MakeUpdate("B", 0.0);
    na_b.status = SignalStatus::NotAvailable;
    updates.push_back(na_b);
    updates.push_back(MakeUpdate("C", 30.0));
    vss_signals = processor->process_signal_updates(updates);
    
    derived_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Derived"; });
    ASSERT_NE(derived_it, vss_signals.end());
    EXPECT_EQ(derived_it->value, "B_NOT_AVAILABLE");
}

// Test filter strategies (STRATEGY_HOLD and STRATEGY_HOLD_TIMEOUT)
TEST_F(SignalProcessorTest, FilterStrategies) {
    // Test STRATEGY_HOLD - should return last valid value when input is invalid
    SignalMapping hold_mapping;
    hold_mapping.source.type = "dbc";
    hold_mapping.source.name = "HoldSignal";
    hold_mapping.datatype = VSSDataType::Double;
    hold_mapping.transform = CodeTransform{"lowpass(x, 0.5, STRATEGY_HOLD)"};
    mappings["Hold.Signal"] = hold_mapping;
    
    // Test STRATEGY_HOLD_TIMEOUT - should hold for a while then return nil
    SignalMapping timeout_mapping;
    timeout_mapping.source.type = "dbc";
    timeout_mapping.source.name = "TimeoutSignal";
    timeout_mapping.datatype = VSSDataType::Double;
    timeout_mapping.transform = CodeTransform{"lowpass(x, 0.5, STRATEGY_HOLD_TIMEOUT)"};
    mappings["Timeout.Signal"] = timeout_mapping;
    
    // Test STRATEGY_PROPAGATE (default) - should return nil immediately
    SignalMapping propagate_mapping;
    propagate_mapping.source.type = "dbc";
    propagate_mapping.source.name = "PropagateSignal";
    propagate_mapping.datatype = VSSDataType::Double;
    propagate_mapping.transform = CodeTransform{"lowpass(x, 0.5)"};  // Default is STRATEGY_PROPAGATE
    mappings["Propagate.Signal"] = propagate_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Send initial valid values to establish filter state
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Hold.Signal", 100.0));
    updates.push_back(MakeUpdate("Timeout.Signal", 200.0));
    updates.push_back(MakeUpdate("Propagate.Signal", 300.0));
    auto vss_signals = processor->process_signal_updates(updates);
    
    // Store last valid values
    double hold_last_valid = 0;
    double timeout_last_valid = 0;
    for (const auto& sig : vss_signals) {
        if (sig.path == "Hold.Signal") hold_last_valid = std::stod(sig.value);
        if (sig.path == "Timeout.Signal") timeout_last_valid = std::stod(sig.value);
    }
    
    // Send another valid update to move the filter
    updates.clear();
    updates.push_back(MakeUpdate("Hold.Signal", 110.0));
    updates.push_back(MakeUpdate("Timeout.Signal", 210.0));
    updates.push_back(MakeUpdate("Propagate.Signal", 310.0));
    vss_signals = processor->process_signal_updates(updates);
    
    for (const auto& sig : vss_signals) {
        if (sig.path == "Hold.Signal") hold_last_valid = std::stod(sig.value);
        if (sig.path == "Timeout.Signal") timeout_last_valid = std::stod(sig.value);
    }
    
    // Now send invalid signals
    updates.clear();
    SignalUpdate invalid_hold = MakeUpdate("Hold.Signal", 0.0);
    invalid_hold.status = SignalStatus::Invalid;
    updates.push_back(invalid_hold);
    
    SignalUpdate invalid_timeout = MakeUpdate("Timeout.Signal", 0.0);
    invalid_timeout.status = SignalStatus::Invalid;
    updates.push_back(invalid_timeout);
    
    SignalUpdate invalid_propagate = MakeUpdate("Propagate.Signal", 0.0);
    invalid_propagate.status = SignalStatus::Invalid;
    updates.push_back(invalid_propagate);
    
    vss_signals = processor->process_signal_updates(updates);
    
    // Check behaviors:
    // STRATEGY_HOLD should output last valid value with invalid status
    auto hold_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Hold.Signal"; });
    ASSERT_NE(hold_it, vss_signals.end());
    EXPECT_EQ(hold_it->status, SignalStatus::Invalid);
    EXPECT_NEAR(std::stod(hold_it->value), hold_last_valid, 0.1);  // Should be close to last valid
    
    // STRATEGY_HOLD_TIMEOUT should also hold initially
    auto timeout_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Timeout.Signal"; });
    ASSERT_NE(timeout_it, vss_signals.end());
    EXPECT_EQ(timeout_it->status, SignalStatus::Invalid);
    EXPECT_NEAR(std::stod(timeout_it->value), timeout_last_valid, 0.1);  // Should hold initially
    
    // STRATEGY_PROPAGATE should have invalid status and N/A value
    auto propagate_it = std::find_if(vss_signals.begin(), vss_signals.end(),
        [](const VSSSignal& s) { return s.path == "Propagate.Signal"; });
    ASSERT_NE(propagate_it, vss_signals.end());
    EXPECT_EQ(propagate_it->status, SignalStatus::Invalid);
    EXPECT_EQ(propagate_it->value, "N/A");  // nil becomes N/A
    
    // Send multiple invalid updates to test hold behavior
    for (int i = 0; i < 3; i++) {
        updates.clear();
        updates.push_back(invalid_hold);
        updates.push_back(invalid_timeout);
        updates.push_back(invalid_propagate);
        vss_signals = processor->process_signal_updates(updates);
        
        // STRATEGY_HOLD should continue returning last valid
        hold_it = std::find_if(vss_signals.begin(), vss_signals.end(),
            [](const VSSSignal& s) { return s.path == "Hold.Signal"; });
        ASSERT_NE(hold_it, vss_signals.end());
        EXPECT_NEAR(std::stod(hold_it->value), hold_last_valid, 0.1);
    }
    
    // Note: Testing actual timeout would require mocking time or sleeping,
    // which we skip for unit tests. The timeout behavior is tested manually.
}

// Test lowpass filter with invalid signals
TEST_F(SignalProcessorTest, LowpassWithInvalidSignals) {
    SignalMapping temp_mapping;
    temp_mapping.source.type = "dbc";
    temp_mapping.source.name = "EngineTemp";
    temp_mapping.datatype = VSSDataType::Double;
    temp_mapping.transform = CodeTransform{"lowpass(x, 0.3)"};
    mappings["Engine.Temperature"] = temp_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Send valid temperature readings
    std::vector<SignalUpdate> updates;
    updates.push_back(MakeUpdate("Engine.Temperature", 70.0));
    auto vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    double first_value = std::stod(vss_signals[0].value);
    
    // Send another valid reading
    updates.clear();
    updates.push_back(MakeUpdate("Engine.Temperature", 80.0));
    vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    double second_value = std::stod(vss_signals[0].value);
    EXPECT_GT(second_value, first_value);  // Should increase toward 80
    EXPECT_LT(second_value, 80.0);  // But not reach 80 due to filter
    
    // Send invalid reading - filter should output with invalid status
    updates.clear();
    SignalUpdate invalid_temp = MakeUpdate("Engine.Temperature", 255.0);  // Error value
    invalid_temp.status = SignalStatus::Invalid;
    updates.push_back(invalid_temp);
    vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].status, SignalStatus::Invalid);
    
    // Send valid reading again - filter should continue from last valid
    updates.clear();
    updates.push_back(MakeUpdate("Engine.Temperature", 75.0));
    vss_signals = processor->process_signal_updates(updates);
    EXPECT_EQ(vss_signals.size(), 1);
    EXPECT_EQ(vss_signals[0].status, SignalStatus::Valid);
    double resumed_value = std::stod(vss_signals[0].value);
    // Filter should move from last valid value toward new input
    // The exact behavior depends on filter alpha and state retention
}