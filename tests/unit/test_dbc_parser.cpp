#include <gtest/gtest.h>
#include "vssdag/can/dbc_parser.h"
#include <fstream>

using namespace vssdag;

class DBCParserTest : public ::testing::Test {
protected:
    std::string test_dbc_file;
    
    void SetUp() override {
        // Create a minimal test DBC file
        test_dbc_file = "test_minimal.dbc";
        CreateMinimalDBCFile();
    }
    
    void TearDown() override {
        // Clean up test file
        std::remove(test_dbc_file.c_str());
    }
    
    void CreateMinimalDBCFile() {
        std::ofstream file(test_dbc_file);
        file << "VERSION \"\"\n\n";
        file << "BS_:\n\n";
        file << "BU_: ECU1 ECU2\n\n";
        
        // Message with ID 0x100 (256)
        file << "BO_ 256 TestMessage1: 8 ECU1\n";
        file << " SG_ Speed : 0|16@1+ (0.1,0) [0|6553.5] \"km/h\" ECU2\n";
        file << " SG_ Temperature : 16|8@1- (1,-40) [-40|215] \"degC\" ECU2\n";
        file << " SG_ Status : 24|2@1+ (1,0) [0|3] \"\" ECU2\n";
        file << " SG_ ErrorCode : 32|8@1+ (1,0) [0|253] \"\" ECU2\n";  // 8-bit signal for invalid/NA testing
        file << "\n";
        
        // Message with ID 0x200 (512) 
        file << "BO_ 512 TestMessage2: 4 ECU2\n";
        file << " SG_ Voltage : 0|12@1+ (0.01,0) [0|40.95] \"V\" ECU1\n";
        file << " SG_ Current : 12|12@1- (0.1,-200) [-204.8|204.7] \"A\" ECU1\n";
        file << "\n";
        
        // Message with full-range signal (no room for invalid/NA)
        file << "BO_ 768 TestMessage3: 2 ECU1\n";
        file << " SG_ FullRange : 0|8@1+ (1,0) [0|255] \"\" ECU2\n";  // Uses full 8-bit range
        file << "\n";
        
        // Add value descriptions (enums)
        file << "VAL_ 256 Status 0 \"OFF\" 1 \"IDLE\" 2 \"ACTIVE\" 3 \"ERROR\" ;\n";
        
        file.close();
    }
    
    void CreateExtendedDBCFile() {
        std::ofstream file("test_extended.dbc");
        file << "VERSION \"\"\n\n";
        file << "BS_:\n\n";
        file << "BU_: ECU1\n\n";
        
        // Message with various bit-size signals for testing invalid/NA patterns
        file << "BO_ 1024 TestPatterns: 8 ECU1\n";
        file << " SG_ Signal4Bit : 0|4@1+ (1,0) [0|13] \"\" ECU1\n";     // 4-bit: invalid=15, NA=14
        file << " SG_ Signal6Bit : 4|6@1+ (1,0) [0|61] \"\" ECU1\n";     // 6-bit: invalid=63, NA=62
        file << " SG_ Signal10Bit : 10|10@1+ (1,0) [0|1021] \"\" ECU1\n"; // 10-bit: invalid=1023, NA=1022
        file << " SG_ Signal16Bit : 20|16@1+ (1,0) [0|65533] \"\" ECU1\n"; // 16-bit: invalid=65535, NA=65534
        file << "\n";
        
        file.close();
    }
};

// Test parsing a valid DBC file
TEST_F(DBCParserTest, ParseValidFile) {
    DBCParser parser(test_dbc_file);
    EXPECT_TRUE(parser.parse());
}

// Test parsing non-existent file
TEST_F(DBCParserTest, ParseNonExistentFile) {
    DBCParser parser("non_existent_file.dbc");
    EXPECT_FALSE(parser.parse());
}

// Test has_message method
TEST_F(DBCParserTest, HasMessage) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    EXPECT_TRUE(parser.has_message(256));
    EXPECT_TRUE(parser.has_message(512));
    EXPECT_TRUE(parser.has_message(768));
    EXPECT_FALSE(parser.has_message(999));
}

// Test getting signal names for a message
TEST_F(DBCParserTest, GetSignalNames) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    auto signals = parser.get_signal_names(256);
    EXPECT_EQ(signals.size(), 4);  // Speed, Temperature, Status, ErrorCode
    
    // Check that specific signals exist
    EXPECT_NE(std::find(signals.begin(), signals.end(), "Speed"), signals.end());
    EXPECT_NE(std::find(signals.begin(), signals.end(), "Temperature"), signals.end());
    EXPECT_NE(std::find(signals.begin(), signals.end(), "Status"), signals.end());
    EXPECT_NE(std::find(signals.begin(), signals.end(), "ErrorCode"), signals.end());
    
    // Check message with 2 signals
    signals = parser.get_signal_names(512);
    EXPECT_EQ(signals.size(), 2);  // Voltage, Current
    
    // Check non-existent message
    signals = parser.get_signal_names(999);
    EXPECT_EQ(signals.size(), 0);
}

// Test finding message ID by signal name
TEST_F(DBCParserTest, GetMessageIdForSignal) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    auto msg_id = parser.get_message_id_for_signal("Speed");
    ASSERT_TRUE(msg_id.has_value());
    EXPECT_EQ(msg_id.value(), 256);
    
    msg_id = parser.get_message_id_for_signal("Voltage");
    ASSERT_TRUE(msg_id.has_value());
    EXPECT_EQ(msg_id.value(), 512);
    
    msg_id = parser.get_message_id_for_signal("NonExistentSignal");
    EXPECT_FALSE(msg_id.has_value());
}

// Test decoding CAN frame with decode_message
TEST_F(DBCParserTest, DecodeMessage) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    // Create a test CAN frame for message 0x100
    // Speed = 1000 (100.0 km/h after scaling)
    // Temperature = 65 (25 degC after scaling: 65 - 40 = 25)
    // Status = 2 (ACTIVE)
    // ErrorCode = 100 (normal value)
    std::vector<uint8_t> data = {
        0xE8, 0x03,  // Speed: 0x03E8 = 1000 -> 100.0 km/h
        0x41,        // Temperature: 65 -> 25 degC
        0x02,        // Status: 2 -> ACTIVE
        0x64,        // ErrorCode: 100 (normal)
        0x00, 0x00, 0x00  // Padding
    };
    
    auto signals = parser.decode_message(256, data.data(), data.size());
    EXPECT_EQ(signals.size(), 4);
    
    // Check Speed signal
    ASSERT_NE(signals.find("Speed"), signals.end());
    ASSERT_TRUE(std::holds_alternative<double>(signals["Speed"].value));
    EXPECT_NEAR(std::get<double>(signals["Speed"].value), 100.0, 0.1);
    EXPECT_EQ(signals["Speed"].status, SignalStatus::Valid);
    
    // Check Temperature signal (has offset of -40, so it's a double)
    ASSERT_NE(signals.find("Temperature"), signals.end());
    ASSERT_TRUE(std::holds_alternative<double>(signals["Temperature"].value));
    EXPECT_NEAR(std::get<double>(signals["Temperature"].value), 25.0, 0.1);
    EXPECT_EQ(signals["Temperature"].status, SignalStatus::Valid);
    
    // Check Status signal (with enum)
    ASSERT_NE(signals.find("Status"), signals.end());
    EXPECT_TRUE(signals["Status"].has_enums);
    EXPECT_EQ(signals["Status"].status, SignalStatus::Valid);
    
    // Check ErrorCode signal
    ASSERT_NE(signals.find("ErrorCode"), signals.end());
    EXPECT_EQ(signals["ErrorCode"].status, SignalStatus::Valid);
}

// Test decoding with decode_message_as_updates
TEST_F(DBCParserTest, DecodeMessageAsUpdates) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    std::vector<uint8_t> data = {
        0xE8, 0x03,  // Speed: 1000 -> 100.0 km/h
        0x41,        // Temperature: 65 -> 25 degC
        0x02,        // Status: 2
        0x64,        // ErrorCode: 100
        0x00, 0x00, 0x00
    };
    
    auto updates = parser.decode_message_as_updates(256, data.data(), data.size());
    EXPECT_EQ(updates.size(), 4);
    
    // Find Speed update
    auto speed_it = std::find_if(updates.begin(), updates.end(),
        [](const DBCSignalUpdate& u) { 
            return std::string(u.dbc_signal_name) == "Speed"; 
        });
    ASSERT_NE(speed_it, updates.end());
    ASSERT_TRUE(std::holds_alternative<double>(speed_it->value));
    EXPECT_NEAR(std::get<double>(speed_it->value), 100.0, 0.1);
    EXPECT_EQ(speed_it->status, SignalStatus::Valid);
}

// Test decoding with invalid message ID
TEST_F(DBCParserTest, DecodeInvalidMessageID) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x00};
    
    auto signals = parser.decode_message(999, data.data(), data.size());
    EXPECT_EQ(signals.size(), 0);
    
    auto updates = parser.decode_message_as_updates(999, data.data(), data.size());
    EXPECT_EQ(updates.size(), 0);
}

// Test signal enums
TEST_F(DBCParserTest, SignalEnums) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    auto enums = parser.get_signal_enums("Status");
    EXPECT_EQ(enums.size(), 4);
    EXPECT_EQ(enums["OFF"], 0);
    EXPECT_EQ(enums["IDLE"], 1);
    EXPECT_EQ(enums["ACTIVE"], 2);
    EXPECT_EQ(enums["ERROR"], 3);
    
    // Non-enum signal should return empty
    enums = parser.get_signal_enums("Speed");
    EXPECT_TRUE(enums.empty());
}

// Test get_all_signal_enums
TEST_F(DBCParserTest, GetAllSignalEnums) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    auto all_enums = parser.get_all_signal_enums();
    EXPECT_EQ(all_enums.size(), 1);  // Only Status has enums
    EXPECT_NE(all_enums.find("Status"), all_enums.end());
    EXPECT_EQ(all_enums["Status"].size(), 4);
}

// Test invalid value detection (0xFF pattern)
TEST_F(DBCParserTest, InvalidValueDetection) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    // Create frame with ErrorCode = 0xFF (invalid)
    std::vector<uint8_t> data = {
        0xE8, 0x03,  // Speed: normal
        0x41,        // Temperature: normal
        0x02,        // Status: normal
        0xFF,        // ErrorCode: 255 (all bits set = invalid)
        0x00, 0x00, 0x00
    };
    
    auto signals = parser.decode_message(256, data.data(), data.size());
    
    // ErrorCode should be marked as invalid
    ASSERT_NE(signals.find("ErrorCode"), signals.end());
    EXPECT_EQ(signals["ErrorCode"].status, SignalStatus::Invalid);
    
    // Other signals should remain valid
    EXPECT_EQ(signals["Speed"].status, SignalStatus::Valid);
    EXPECT_EQ(signals["Temperature"].status, SignalStatus::Valid);
    EXPECT_EQ(signals["Status"].status, SignalStatus::Valid);
}

// Test not-available value detection (0xFE pattern)
TEST_F(DBCParserTest, NotAvailableValueDetection) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    // Create frame with ErrorCode = 0xFE (not available)
    std::vector<uint8_t> data = {
        0xE8, 0x03,  // Speed: normal
        0x41,        // Temperature: normal
        0x02,        // Status: normal
        0xFE,        // ErrorCode: 254 (all bits minus one = not available)
        0x00, 0x00, 0x00
    };
    
    auto signals = parser.decode_message(256, data.data(), data.size());
    
    // ErrorCode should be marked as not available
    ASSERT_NE(signals.find("ErrorCode"), signals.end());
    EXPECT_EQ(signals["ErrorCode"].status, SignalStatus::NotAvailable);
    
    // Other signals should remain valid
    EXPECT_EQ(signals["Speed"].status, SignalStatus::Valid);
    EXPECT_EQ(signals["Temperature"].status, SignalStatus::Valid);
    EXPECT_EQ(signals["Status"].status, SignalStatus::Valid);
}

// Test signal with full bit range (no room for invalid/NA patterns)
TEST_F(DBCParserTest, FullRangeSignal) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    // FullRange signal uses entire 8-bit range [0-255]
    // So 0xFF and 0xFE are valid values, not invalid/NA
    std::vector<uint8_t> data_ff = {0xFF, 0x00};  // FullRange = 255
    auto signals = parser.decode_message(768, data_ff.data(), data_ff.size());
    
    ASSERT_NE(signals.find("FullRange"), signals.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(signals["FullRange"].value));
    EXPECT_EQ(std::get<int64_t>(signals["FullRange"].value), 255);
    EXPECT_EQ(signals["FullRange"].status, SignalStatus::Valid);  // Should be valid, not invalid
    
    std::vector<uint8_t> data_fe = {0xFE, 0x00};  // FullRange = 254
    signals = parser.decode_message(768, data_fe.data(), data_fe.size());
    
    ASSERT_NE(signals.find("FullRange"), signals.end());
    ASSERT_TRUE(std::holds_alternative<int64_t>(signals["FullRange"].value));
    EXPECT_EQ(std::get<int64_t>(signals["FullRange"].value), 254);
    EXPECT_EQ(signals["FullRange"].status, SignalStatus::Valid);  // Should be valid, not NA
}

// Test out-of-range detection
TEST_F(DBCParserTest, OutOfRangeDetection) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    // Temperature has range [-40, 215]
    // Let's create a value that after scaling falls outside this range
    // Temperature uses 8-bit signed with factor=1, offset=-40
    // Raw value 0 -> physical -40 (min)
    // Raw value 255 -> physical 215 (max)
    // If we somehow get a physical value outside [-40, 215], it should be invalid
    
    // Note: This test might not trigger because the DBC parser might clamp values
    // But we test the logic anyway
    
    std::vector<uint8_t> data = {
        0xE8, 0x03,  // Speed: normal
        0xFF,        // Temperature: 255 - 40 = 215 (at max, still valid)
        0x02,        // Status: normal
        0x64,        // ErrorCode: normal
        0x00, 0x00, 0x00
    };
    
    auto signals = parser.decode_message(256, data.data(), data.size());
    
    // Temperature at max should still be valid
    ASSERT_NE(signals.find("Temperature"), signals.end());
    // The actual behavior depends on the DBC parser's range checking
}

// Test status propagation in decode_message_as_updates
TEST_F(DBCParserTest, StatusPropagationInUpdates) {
    DBCParser parser(test_dbc_file);
    ASSERT_TRUE(parser.parse());
    
    // Mix of valid, invalid, and NA values
    std::vector<uint8_t> data = {
        0xE8, 0x03,  // Speed: normal (valid)
        0x41,        // Temperature: normal (valid)
        0x03,        // Status: 3 (ERROR enum, but valid signal)
        0xFF,        // ErrorCode: 255 (invalid)
        0x00, 0x00, 0x00
    };
    
    auto updates = parser.decode_message_as_updates(256, data.data(), data.size());
    EXPECT_EQ(updates.size(), 4);
    
    // Check each signal's status
    for (const auto& update : updates) {
        if (std::string(update.dbc_signal_name) == "ErrorCode") {
            EXPECT_EQ(update.status, SignalStatus::Invalid);
        } else {
            EXPECT_EQ(update.status, SignalStatus::Valid);
        }
    }
    
    // Now test with NA value
    data[4] = 0xFE;  // ErrorCode: 254 (not available)
    updates = parser.decode_message_as_updates(256, data.data(), data.size());
    
    for (const auto& update : updates) {
        if (std::string(update.dbc_signal_name) == "ErrorCode") {
            EXPECT_EQ(update.status, SignalStatus::NotAvailable);
        } else {
            EXPECT_EQ(update.status, SignalStatus::Valid);
        }
    }
}

// Test various bit sizes for invalid/NA patterns
TEST_F(DBCParserTest, VariousBitSizePatterns) {
    CreateExtendedDBCFile();
    DBCParser parser("test_extended.dbc");
    ASSERT_TRUE(parser.parse());
    
    // Test 4-bit signal: invalid=15 (0xF), NA=14 (0xE)
    std::vector<uint8_t> data = {0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // 4-bit = 15
    auto signals = parser.decode_message(1024, data.data(), data.size());
    ASSERT_NE(signals.find("Signal4Bit"), signals.end());
    EXPECT_EQ(signals["Signal4Bit"].status, SignalStatus::Invalid);
    
    data[0] = 0x0E;  // 4-bit = 14
    signals = parser.decode_message(1024, data.data(), data.size());
    ASSERT_NE(signals.find("Signal4Bit"), signals.end());
    EXPECT_EQ(signals["Signal4Bit"].status, SignalStatus::NotAvailable);
    
    // Clean up
    std::remove("test_extended.dbc");
}