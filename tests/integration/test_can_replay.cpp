#include <gtest/gtest.h>
#include "vssdag/signal_processor.h"
#include "vssdag/can/dbc_parser.h"
#include "vssdag/mapping_types.h"
#include <fstream>
#include <sstream>
#include <regex>

using namespace vssdag;

class CANReplayTest : public ::testing::Test {
protected:
    std::unique_ptr<SignalProcessorDAG> processor;
    std::unique_ptr<DBCParser> dbc_parser;
    std::unordered_map<std::string, SignalMapping> mappings;
    
    void SetUp() override {
        processor = std::make_unique<SignalProcessorDAG>();
        // Test data will be in build/tests/data/
    }
    
    void TearDown() override {
        processor.reset();
        dbc_parser.reset();
    }
    
    // Parse a line from candump format
    // Example: (1621000000.123456) can0 100#0102030405060708
    bool ParseCandumpLine(const std::string& line, 
                          uint32_t& can_id, 
                          std::vector<uint8_t>& data,
                          double& timestamp) {
        std::regex candump_regex(R"(\(([0-9]+\.[0-9]+)\)\s+\w+\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]+))");
        std::smatch matches;
        
        if (std::regex_search(line, matches, candump_regex)) {
            timestamp = std::stod(matches[1]);
            can_id = std::stoul(matches[2], nullptr, 16);
            
            std::string hex_data = matches[3];
            data.clear();
            for (size_t i = 0; i < hex_data.length(); i += 2) {
                std::string byte_str = hex_data.substr(i, 2);
                data.push_back(std::stoul(byte_str, nullptr, 16));
            }
            return true;
        }
        return false;
    }
};

// Test replaying a simple CAN log
TEST_F(CANReplayTest, SimpleLogReplay) {
    // Create a simple test log in memory
    std::stringstream test_log;
    test_log << "(1621000000.100000) can0 100#0A00000000000000\n";  // Speed = 10
    test_log << "(1621000000.200000) can0 100#1400000000000000\n";  // Speed = 20
    test_log << "(1621000000.300000) can0 100#1E00000000000000\n";  // Speed = 30
    
    // Create simple DBC-like setup
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = VSSDataType::Double;
    speed_mapping.transform = CodeTransform{"x"};
    mappings["Vehicle.Speed"] = speed_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Process log lines
    std::string line;
    std::vector<double> speeds_collected;
    
    while (std::getline(test_log, line)) {
        uint32_t can_id;
        std::vector<uint8_t> data;
        double timestamp;
        
        if (ParseCandumpLine(line, can_id, data, timestamp)) {
            if (can_id == 0x100) {
                // Simulate extracting speed from first byte
                double speed = data[0];
                
                std::vector<SignalUpdate> updates;
                SignalUpdate update;
                update.signal_name = "Vehicle.Speed";
                update.value = speed;
                update.timestamp = std::chrono::steady_clock::now();
                updates.push_back(update);
                
                auto vss_signals = processor->process_signal_updates(updates);
                
                for (const auto& signal : vss_signals) {
                    if (signal.path == "Vehicle.Speed") {
                        speeds_collected.push_back(std::stod(signal.value));
                    }
                }
            }
        }
    }
    
    // Verify we got the expected speeds
    ASSERT_EQ(speeds_collected.size(), 3);
    EXPECT_DOUBLE_EQ(speeds_collected[0], 10.0);
    EXPECT_DOUBLE_EQ(speeds_collected[1], 20.0);
    EXPECT_DOUBLE_EQ(speeds_collected[2], 30.0);
}

// Test with derived signals during replay
TEST_F(CANReplayTest, DerivedSignalsReplay) {
    // Create test log with speed and throttle
    std::stringstream test_log;
    test_log << "(1621000000.100000) can0 100#3200000000000000\n";  // Speed = 50
    test_log << "(1621000000.100000) can0 200#5000000000000000\n";  // Throttle = 80
    test_log << "(1621000000.200000) can0 100#6400000000000000\n";  // Speed = 100
    test_log << "(1621000000.200000) can0 200#6400000000000000\n";  // Throttle = 100
    
    // Setup mappings
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
    
    // Derived signal: driving mode based on speed and throttle
    SignalMapping mode_mapping;
    mode_mapping.depends_on.push_back("Vehicle.Speed");
    mode_mapping.depends_on.push_back("Vehicle.Throttle");
    mode_mapping.datatype = VSSDataType::String;
    mode_mapping.transform = CodeTransform{
        "local speed = deps['Vehicle.Speed']\n"
        "local throttle = deps['Vehicle.Throttle']\n"
        "if speed > 80 and throttle > 90 then return 'SPORT'\n"
        "elseif speed < 60 and throttle < 50 then return 'ECO'\n"
        "else return 'NORMAL' end"
    };
    mappings["Vehicle.DrivingMode"] = mode_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Process log
    std::string line;
    std::map<double, std::string> modes_by_time;
    std::map<double, std::vector<SignalUpdate>> updates_by_time;
    
    // First pass: collect updates by timestamp
    while (std::getline(test_log, line)) {
        uint32_t can_id;
        std::vector<uint8_t> data;
        double timestamp;
        
        if (ParseCandumpLine(line, can_id, data, timestamp)) {
            if (can_id == 0x100) {
                SignalUpdate update;
                update.signal_name = "Vehicle.Speed";
                update.value = (double)data[0];
                update.timestamp = std::chrono::steady_clock::now();
                updates_by_time[timestamp].push_back(update);
            } else if (can_id == 0x200) {
                SignalUpdate update;
                update.signal_name = "Vehicle.Throttle";
                update.value = (double)data[0];
                update.timestamp = std::chrono::steady_clock::now();
                updates_by_time[timestamp].push_back(update);
            }
        }
    }
    
    // Second pass: process updates grouped by timestamp
    for (const auto& [timestamp, updates] : updates_by_time) {
        auto vss_signals = processor->process_signal_updates(updates);
        
        for (const auto& signal : vss_signals) {
            if (signal.path == "Vehicle.DrivingMode") {
                modes_by_time[timestamp] = signal.value;
            }
        }
    }
    
    // Verify driving modes
    ASSERT_EQ(modes_by_time.size(), 2);
    EXPECT_EQ(modes_by_time[1621000000.1], "NORMAL");  // Speed=50, Throttle=80
    EXPECT_EQ(modes_by_time[1621000000.2], "SPORT");   // Speed=100, Throttle=100
}

// Test performance with larger log
TEST_F(CANReplayTest, PerformanceTest) {
    // Generate a larger synthetic log
    std::stringstream test_log;
    const int num_messages = 1000;
    
    for (int i = 0; i < num_messages; ++i) {
        double timestamp = 1621000000.0 + (i * 0.01);  // 10ms intervals
        uint8_t speed = (i % 200);  // Speed cycles 0-199
        
        test_log << "(" << std::fixed << std::setprecision(6) << timestamp 
                 << ") can0 100#" << std::hex << std::setfill('0') << std::setw(2) 
                 << (int)speed << "00000000000000\n";
    }
    
    // Simple mapping
    SignalMapping speed_mapping;
    speed_mapping.source.type = "dbc";
    speed_mapping.source.name = "VehicleSpeed";
    speed_mapping.datatype = VSSDataType::Double;
    speed_mapping.transform = CodeTransform{"x * 3.6"};  // Convert to km/h
    mappings["Vehicle.Speed"] = speed_mapping;
    
    ASSERT_TRUE(processor->initialize(mappings));
    
    // Measure processing time
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string line;
    int processed_count = 0;
    test_log.seekg(0);  // Reset to beginning
    
    while (std::getline(test_log, line)) {
        uint32_t can_id;
        std::vector<uint8_t> data;
        double timestamp;
        
        if (ParseCandumpLine(line, can_id, data, timestamp)) {
            std::vector<SignalUpdate> updates;
            SignalUpdate update;
            update.signal_name = "Vehicle.Speed";
            update.value = (double)data[0];
            update.timestamp = std::chrono::steady_clock::now();
            updates.push_back(update);
            
            auto vss_signals = processor->process_signal_updates(updates);
            processed_count++;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_EQ(processed_count, num_messages);
    
    // Performance expectation: should process 1000 messages in less than 1000ms (1ms per message)
    // This includes Lua processing overhead
    EXPECT_LT(duration.count(), 1000);
    
    // Calculate throughput
    double messages_per_second = (processed_count * 1000.0) / duration.count();
    std::cout << "Performance: " << messages_per_second << " messages/second\n";
}