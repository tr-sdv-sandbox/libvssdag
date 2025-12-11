/// @file test_candump_parser.cpp
/// @brief Unit tests for candump log line parsing
///
/// Tests parsing of candump format lines with both 11-bit standard
/// and 29-bit extended CAN IDs.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

// ============================================================================
// Copy of parse_candump_line from avtp_canplayer for testing
// ============================================================================

struct CanLogEntry {
    double timestamp;
    std::string interface;
    uint32_t can_id;
    bool extended_id;
    std::vector<uint8_t> data;
};

// Parse a candump log line
// Format: (timestamp) interface CAN_ID#DATA_HEX
// Uses sscanf instead of std::regex for cross-platform compatibility
bool parse_candump_line(const std::string& line, CanLogEntry& entry) {
    // Skip empty lines and comments
    if (line.empty() || line[0] == '#') {
        return false;
    }

    // Parse using sscanf - more portable than std::regex
    // Format: (timestamp) interface CAN_ID#DATA
    double timestamp;
    char interface[64] = {0};
    char can_id_str[16] = {0};
    char data_str[64] = {0};

    // Try format with data
    int parsed = sscanf(line.c_str(), "(%lf) %63s %15[^#]#%63s",
                        &timestamp, interface, can_id_str, data_str);

    if (parsed < 3) {
        return false;
    }

    entry.timestamp = timestamp;
    entry.interface = interface;

    // Parse CAN ID - if > 0x7FF, it's extended
    entry.can_id = std::stoul(can_id_str, nullptr, 16);
    entry.extended_id = (entry.can_id > 0x7FF);

    // Parse data bytes (parsed == 3 means no data, which is valid)
    entry.data.clear();
    if (parsed >= 4) {
        std::string data(data_str);
        for (size_t i = 0; i + 1 < data.length(); i += 2) {
            uint8_t byte = static_cast<uint8_t>(
                std::stoul(data.substr(i, 2), nullptr, 16));
            entry.data.push_back(byte);
        }
    }

    return true;
}

// ============================================================================
// Tests
// ============================================================================

class CandumpParserTest : public ::testing::Test {
protected:
    CanLogEntry entry;
};

// ---------------------------------------------------------------------------
// 11-bit Standard CAN ID tests
// ---------------------------------------------------------------------------

TEST_F(CandumpParserTest, Parse11BitStandardId) {
    std::string line = "(1597242902.648455) can0 266#0000012000009401";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_DOUBLE_EQ(entry.timestamp, 1597242902.648455);
    EXPECT_EQ(entry.interface, "can0");
    EXPECT_EQ(entry.can_id, 0x266);
    EXPECT_FALSE(entry.extended_id);
    ASSERT_EQ(entry.data.size(), 8);
    EXPECT_EQ(entry.data[0], 0x00);
    EXPECT_EQ(entry.data[1], 0x00);
    EXPECT_EQ(entry.data[2], 0x01);
    EXPECT_EQ(entry.data[3], 0x20);
}

TEST_F(CandumpParserTest, Parse11BitSmallestId) {
    std::string line = "(1234.567890) vcan0 001#AA";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x001);
    EXPECT_FALSE(entry.extended_id);
    ASSERT_EQ(entry.data.size(), 1);
    EXPECT_EQ(entry.data[0], 0xAA);
}

TEST_F(CandumpParserTest, Parse11BitMaxId) {
    // 0x7FF is the maximum 11-bit ID (2047)
    std::string line = "(1234.567890) can0 7FF#DEADBEEF";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x7FF);
    EXPECT_FALSE(entry.extended_id);  // 0x7FF is still standard
    ASSERT_EQ(entry.data.size(), 4);
}

TEST_F(CandumpParserTest, Parse11BitElmcanInterface) {
    // Real format from Tesla Model 3 candump
    std::string line = "(1597242902.652945) elmcan 118#5401221800000000";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.interface, "elmcan");
    EXPECT_EQ(entry.can_id, 0x118);
    EXPECT_FALSE(entry.extended_id);
    ASSERT_EQ(entry.data.size(), 8);
}

// ---------------------------------------------------------------------------
// 29-bit Extended CAN ID tests (J1939)
// ---------------------------------------------------------------------------

TEST_F(CandumpParserTest, Parse29BitExtendedId) {
    // J1939 PGN example - anything > 0x7FF is extended
    std::string line = "(1234.567890) can0 18FEF100#0102030405060708";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x18FEF100);
    EXPECT_TRUE(entry.extended_id);
    ASSERT_EQ(entry.data.size(), 8);
}

TEST_F(CandumpParserTest, Parse29BitSmallestExtendedId) {
    // 0x800 is the first extended ID
    std::string line = "(1234.567890) can0 800#AABB";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x800);
    EXPECT_TRUE(entry.extended_id);
}

TEST_F(CandumpParserTest, Parse29BitMaxExtendedId) {
    // Maximum 29-bit ID: 0x1FFFFFFF
    std::string line = "(1234.567890) can0 1FFFFFFF#00";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x1FFFFFFF);
    EXPECT_TRUE(entry.extended_id);
}

TEST_F(CandumpParserTest, Parse29BitJ1939EngineSpeed) {
    // Real J1939 PGN 61444 (0xF004) - Electronic Engine Controller 1
    // Full ID: 0x0CF00400 (priority 3, PGN 61444, source 0)
    std::string line = "(1597242902.123456) can0 0CF00400#F87D00000000FFFF";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x0CF00400);
    EXPECT_TRUE(entry.extended_id);
    ASSERT_EQ(entry.data.size(), 8);
    EXPECT_EQ(entry.data[0], 0xF8);
    EXPECT_EQ(entry.data[1], 0x7D);
}

// ---------------------------------------------------------------------------
// Edge cases and special formats
// ---------------------------------------------------------------------------

TEST_F(CandumpParserTest, ParseEmptyData) {
    std::string line = "(1234.567890) can0 123#";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0x123);
    EXPECT_EQ(entry.data.size(), 0);
}

TEST_F(CandumpParserTest, ParseSingleByteData) {
    std::string line = "(1234.567890) can0 123#FF";

    ASSERT_TRUE(parse_candump_line(line, entry));
    ASSERT_EQ(entry.data.size(), 1);
    EXPECT_EQ(entry.data[0], 0xFF);
}

TEST_F(CandumpParserTest, ParseLowercaseHex) {
    std::string line = "(1234.567890) can0 abc#deadbeef";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0xABC);
    EXPECT_TRUE(entry.extended_id);  // 0xABC > 0x7FF
    ASSERT_EQ(entry.data.size(), 4);
    EXPECT_EQ(entry.data[0], 0xDE);
    EXPECT_EQ(entry.data[1], 0xAD);
    EXPECT_EQ(entry.data[2], 0xBE);
    EXPECT_EQ(entry.data[3], 0xEF);
}

TEST_F(CandumpParserTest, ParseMixedCaseHex) {
    std::string line = "(1234.567890) can0 AbC#DeAdBeEf";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_EQ(entry.can_id, 0xABC);
}

TEST_F(CandumpParserTest, ParseHighPrecisionTimestamp) {
    std::string line = "(1597242902.123456789) can0 100#00";

    ASSERT_TRUE(parse_candump_line(line, entry));
    EXPECT_NEAR(entry.timestamp, 1597242902.123456789, 0.000001);
}

// ---------------------------------------------------------------------------
// Invalid input tests
// ---------------------------------------------------------------------------

TEST_F(CandumpParserTest, RejectEmptyLine) {
    EXPECT_FALSE(parse_candump_line("", entry));
}

TEST_F(CandumpParserTest, RejectCommentLine) {
    EXPECT_FALSE(parse_candump_line("# This is a comment", entry));
}

TEST_F(CandumpParserTest, RejectMalformedLine) {
    EXPECT_FALSE(parse_candump_line("garbage data", entry));
}

TEST_F(CandumpParserTest, RejectMissingTimestamp) {
    EXPECT_FALSE(parse_candump_line("can0 123#AABB", entry));
}

TEST_F(CandumpParserTest, RejectMissingParentheses) {
    EXPECT_FALSE(parse_candump_line("1234.567890 can0 123#AABB", entry));
}

// ---------------------------------------------------------------------------
// Multiple interfaces test
// ---------------------------------------------------------------------------

TEST_F(CandumpParserTest, ParseVariousInterfaces) {
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"(1234.0) can0 100#AA", "can0"},
        {"(1234.0) can1 100#AA", "can1"},
        {"(1234.0) vcan0 100#AA", "vcan0"},
        {"(1234.0) slcan0 100#AA", "slcan0"},
        {"(1234.0) elmcan 100#AA", "elmcan"},
    };

    for (const auto& [line, expected_interface] : test_cases) {
        ASSERT_TRUE(parse_candump_line(line, entry)) << "Failed for: " << line;
        EXPECT_EQ(entry.interface, expected_interface) << "Failed for: " << line;
    }
}
