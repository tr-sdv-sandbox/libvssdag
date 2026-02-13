#include <gtest/gtest.h>
#include "vssdag/can/can_source.h"
#include "vssdag/mapping_types.h"
#include <fstream>

using namespace vssdag;

class CANSourceTest : public ::testing::Test {
protected:
    std::string test_dbc_file;

    void SetUp() override {
        test_dbc_file = "test_can_source.dbc";
        CreateTestDBCFile();
    }

    void TearDown() override {
        std::remove(test_dbc_file.c_str());
    }

    void CreateTestDBCFile() {
        std::ofstream file(test_dbc_file);
        file << "VERSION \"\"\n\n";
        file << "BS_:\n\n";
        file << "BU_: ECU1 ECU2\n\n";

        // Message with ID 0x100 (256)
        file << "BO_ 256 TestMessage1: 8 ECU1\n";
        file << " SG_ Speed : 0|16@1+ (0.1,0) [0|6553.5] \"km/h\" ECU2\n";
        file << " SG_ Temperature : 16|8@1- (1,-40) [-40|215] \"degC\" ECU2\n";
        file << "\n";

        // Message with ID 0x200 (512)
        file << "BO_ 512 TestMessage2: 4 ECU2\n";
        file << " SG_ Voltage : 0|12@1+ (0.01,0) [0|40.95] \"V\" ECU1\n";
        file << "\n";

        // Message with same signal name in different message
        file << "BO_ 768 TestMessage3: 4 ECU1\n";
        file << " SG_ Speed : 0|16@1+ (0.1,0) [0|6553.5] \"km/h\" ECU2\n";
        file << "\n";

        file.close();
    }

    std::unordered_map<std::string, SignalMapping> CreateTestMappings() {
        std::unordered_map<std::string, SignalMapping> mappings;

        SignalMapping speed_mapping;
        speed_mapping.datatype = ValueType::FLOAT;
        speed_mapping.transform = DirectMapping{};
        speed_mapping.source.type = "dbc";
        speed_mapping.source.name = "TestMessage1.Speed";
        mappings["Vehicle.Speed"] = speed_mapping;

        SignalMapping voltage_mapping;
        voltage_mapping.datatype = ValueType::FLOAT;
        voltage_mapping.transform = DirectMapping{};
        voltage_mapping.source.type = "dbc";
        voltage_mapping.source.name = "TestMessage2.Voltage";
        mappings["Vehicle.Voltage"] = voltage_mapping;

        // Same signal name disambiguated by message
        SignalMapping speed2_mapping;
        speed2_mapping.datatype = ValueType::FLOAT;
        speed2_mapping.transform = DirectMapping{};
        speed2_mapping.source.type = "dbc";
        speed2_mapping.source.name = "TestMessage3.Speed";
        mappings["Vehicle.Speed2"] = speed2_mapping;

        return mappings;
    }
};

// Test that DBC parsing and signal extraction works with qualified names.
// Uses a non-existent CAN interface so initialize() fails at socket open
// (after DBC parsing succeeds), avoiding a blocking reader thread.
TEST_F(CANSourceTest, InitializeWithQualifiedSignals) {
    auto mappings = CreateTestMappings();

    CANSignalSource source(CANTransport::SOCKETCAN, "noexist0", test_dbc_file, mappings);
    source.initialize();

    auto exported = source.get_exported_signals();
    EXPECT_EQ(exported.size(), 3);
    EXPECT_NE(std::find(exported.begin(), exported.end(), "Vehicle.Speed"), exported.end());
    EXPECT_NE(std::find(exported.begin(), exported.end(), "Vehicle.Voltage"), exported.end());
    EXPECT_NE(std::find(exported.begin(), exported.end(), "Vehicle.Speed2"), exported.end());
}

// Test that bare signal names (without message prefix) are rejected
TEST_F(CANSourceTest, BareSignalNameRejected) {
    std::unordered_map<std::string, SignalMapping> mappings;

    SignalMapping mapping;
    mapping.datatype = ValueType::FLOAT;
    mapping.transform = DirectMapping{};
    mapping.source.type = "dbc";
    mapping.source.name = "Speed";  // Missing message prefix
    mappings["Vehicle.Speed"] = mapping;

    CANSignalSource source(CANTransport::SOCKETCAN, "noexist0", test_dbc_file, mappings);

    EXPECT_FALSE(source.initialize());
}

// Test that exported signals match configuration
TEST_F(CANSourceTest, ExportedSignalsMatchConfig) {
    auto mappings = CreateTestMappings();

    CANSignalSource source(CANTransport::SOCKETCAN, "noexist0", test_dbc_file, mappings);
    source.initialize();

    auto exported = source.get_exported_signals();
    std::sort(exported.begin(), exported.end());

    std::vector<std::string> expected = {"Vehicle.Speed", "Vehicle.Speed2", "Vehicle.Voltage"};
    std::sort(expected.begin(), expected.end());

    EXPECT_EQ(exported, expected);
}

// Test with non-existent DBC file
TEST_F(CANSourceTest, NonExistentDBCFile) {
    auto mappings = CreateTestMappings();

    CANSignalSource source(CANTransport::SOCKETCAN, "noexist0", "non_existent.dbc", mappings);

    EXPECT_FALSE(source.initialize());
}

// Test with empty mappings
TEST_F(CANSourceTest, EmptyMappings) {
    std::unordered_map<std::string, SignalMapping> empty_mappings;

    CANSignalSource source(CANTransport::SOCKETCAN, "noexist0", test_dbc_file, empty_mappings);
    source.initialize();

    auto exported = source.get_exported_signals();
    EXPECT_EQ(exported.size(), 0);
}

// Test disambiguating same signal name across messages
TEST_F(CANSourceTest, DisambiguateSameSignalName) {
    std::unordered_map<std::string, SignalMapping> mappings;

    SignalMapping mapping1;
    mapping1.datatype = ValueType::FLOAT;
    mapping1.transform = DirectMapping{};
    mapping1.source.type = "dbc";
    mapping1.source.name = "TestMessage1.Speed";
    mappings["Speed.From.Message1"] = mapping1;

    SignalMapping mapping2;
    mapping2.datatype = ValueType::FLOAT;
    mapping2.transform = DirectMapping{};
    mapping2.source.type = "dbc";
    mapping2.source.name = "TestMessage3.Speed";
    mappings["Speed.From.Message3"] = mapping2;

    CANSignalSource source(CANTransport::SOCKETCAN, "noexist0", test_dbc_file, mappings);
    source.initialize();

    auto exported = source.get_exported_signals();
    EXPECT_EQ(exported.size(), 2);
    EXPECT_NE(std::find(exported.begin(), exported.end(), "Speed.From.Message1"), exported.end());
    EXPECT_NE(std::find(exported.begin(), exported.end(), "Speed.From.Message3"), exported.end());
}
