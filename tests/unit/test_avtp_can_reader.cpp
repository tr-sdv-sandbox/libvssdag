#include <gtest/gtest.h>
#include "vssdag/can/avtp_can_reader.h"
#include <vector>
#include <cstring>

using namespace vssdag;

class AVTPCanReaderTest : public ::testing::Test {
protected:
    AVTPCanReader reader;
    std::vector<CANFrame> received_frames;

    void SetUp() override {
        received_frames.clear();
        reader.set_frame_handler([this](const CANFrame& frame) {
            received_frames.push_back(frame);
        });
    }

    // Helper to build an AVTP ACF CAN packet
    // Returns the AVTP payload (after Ethernet header)
    std::vector<uint8_t> build_avtp_acf_can(
        uint32_t can_id,
        const std::vector<uint8_t>& can_data,
        bool extended_id = false,
        bool rtr = false,
        const uint8_t* stream_id = nullptr
    ) {
        std::vector<uint8_t> packet;

        // AVTP common header (12 bytes)
        packet.push_back(0x7F);  // subtype = ACF (0x7F)
        packet.push_back(0x00);  // sv=0, version=0, mr=0, r=0, gv=0, tv=0
        packet.push_back(0x00);  // sequence_num high
        packet.push_back(0x01);  // sequence_num low

        // Stream ID (8 bytes)
        if (stream_id) {
            for (int i = 0; i < 8; i++) {
                packet.push_back(stream_id[i]);
            }
        } else {
            for (int i = 0; i < 8; i++) {
                packet.push_back(0x00);
            }
        }

        // ACF message header
        // Byte 0: acf_msg_type (7 bits) + acf_msg_length MSB (1 bit)
        // Byte 1: acf_msg_length LSB (8 bits)
        // ACF type 0x02 = CAN, length in quadlets
        uint8_t acf_type = 0x02;  // CAN message
        size_t acf_data_size = 8 + can_data.size();  // Header + CAN data
        // Pad to quadlet boundary
        size_t padded_size = (acf_data_size + 3) & ~3;
        uint16_t acf_len_quadlets = padded_size / 4;

        packet.push_back((acf_type << 1) | ((acf_len_quadlets >> 8) & 0x01));
        packet.push_back(acf_len_quadlets & 0xFF);

        // ACF CAN message format:
        // Byte 0: pad(2) + mtv(1) + rtr(1) + eff(1) + brs(1) + fdf(1) + r(1)
        uint8_t flags = 0;
        if (extended_id) flags |= 0x08;  // eff bit
        if (rtr) flags |= 0x10;          // rtr bit
        packet.push_back(flags);

        // Bytes 1-4: CAN ID
        if (extended_id) {
            // 29-bit extended ID
            packet.push_back((can_id >> 24) & 0x1F);
            packet.push_back((can_id >> 16) & 0xFF);
            packet.push_back((can_id >> 8) & 0xFF);
            packet.push_back(can_id & 0xFF);
        } else {
            // 11-bit standard ID in bytes 2-3
            packet.push_back(0x00);
            packet.push_back((can_id >> 8) & 0x07);
            packet.push_back(can_id & 0xFF);
            packet.push_back(can_data.size() & 0x0F);  // DLC
        }

        // For extended ID, DLC is in byte 4
        if (extended_id) {
            packet.push_back(can_data.size() & 0x0F);  // DLC
            packet.push_back(0x00);  // padding
            packet.push_back(0x00);
            packet.push_back(0x00);
        } else {
            // Standard ID already has DLC, add padding
            packet.push_back(0x00);
            packet.push_back(0x00);
            packet.push_back(0x00);
        }

        // CAN data
        for (uint8_t b : can_data) {
            packet.push_back(b);
        }

        // Pad to quadlet boundary
        while (packet.size() % 4 != 0) {
            packet.push_back(0x00);
        }

        return packet;
    }
};

// Test default construction
TEST_F(AVTPCanReaderTest, DefaultConstruction) {
    AVTPCanReader r;
    EXPECT_FALSE(r.is_open());
}

// Test construction with config
TEST_F(AVTPCanReaderTest, ConstructionWithConfig) {
    AVTPCanReader::Config config;
    config.ethertype = 0x22F0;
    config.filter_stream_id = false;

    AVTPCanReader r(config);
    EXPECT_FALSE(r.is_open());
}

// Test statistics initialization
TEST_F(AVTPCanReaderTest, StatisticsInitialization) {
    auto stats = reader.get_stats();
    EXPECT_EQ(stats.packets_received, 0);
    EXPECT_EQ(stats.can_frames_extracted, 0);
    EXPECT_EQ(stats.invalid_packets, 0);
    EXPECT_EQ(stats.filtered_packets, 0);
}

// Test opening on non-existent interface fails gracefully
// Note: This test will fail if run as root with CAP_NET_RAW
TEST_F(AVTPCanReaderTest, OpenNonExistentInterface) {
    // This should fail because the interface doesn't exist
    // (and we likely don't have CAP_NET_RAW anyway)
    bool result = reader.open("nonexistent_interface_xyz123");
    // Either fails due to no CAP_NET_RAW or interface not found
    EXPECT_FALSE(result);
    EXPECT_FALSE(reader.is_open());
}

// Test frame handler setting
TEST_F(AVTPCanReaderTest, FrameHandlerSetting) {
    int callback_count = 0;
    reader.set_frame_handler([&callback_count](const CANFrame&) {
        callback_count++;
    });

    // Can't easily test without a real socket, but at least verify setup works
    EXPECT_EQ(callback_count, 0);
}

// Test stop flag
TEST_F(AVTPCanReaderTest, StopFlag) {
    reader.stop();
    // Should be able to call stop without being open
    // (read_loop would exit immediately if not open)
}

// Test packet parsing logic via direct method access
// We can test parse_acf_can indirectly through the frame handler
// Note: These tests would need friend access or a test wrapper to test directly

// Test config stream ID filter setup
TEST_F(AVTPCanReaderTest, StreamIdFilterConfig) {
    AVTPCanReader::Config config;
    config.filter_stream_id = true;
    config.stream_id[0] = 0x01;
    config.stream_id[1] = 0x02;
    config.stream_id[7] = 0x08;

    AVTPCanReader r(config);
    // Just verify construction works with filter enabled
    EXPECT_FALSE(r.is_open());
}

// Test ethertype config
TEST_F(AVTPCanReaderTest, EthertypeConfig) {
    AVTPCanReader::Config config;
    config.ethertype = 0x88B5;  // Alternative ethertype

    AVTPCanReader r(config);
    EXPECT_FALSE(r.is_open());
}

// Integration-style test: build a packet and verify structure
TEST_F(AVTPCanReaderTest, PacketBuildHelper) {
    std::vector<uint8_t> can_data = {0x01, 0x02, 0x03, 0x04};
    auto packet = build_avtp_acf_can(0x123, can_data, false, false);

    // Verify packet structure
    EXPECT_GE(packet.size(), 12 + 2 + 8);  // AVTP header + ACF header + CAN header

    // Check AVTP subtype
    EXPECT_EQ(packet[0], 0x7F);  // ACF subtype
}

// Test packet with extended CAN ID
TEST_F(AVTPCanReaderTest, PacketBuildExtendedId) {
    std::vector<uint8_t> can_data = {0x11, 0x22};
    auto packet = build_avtp_acf_can(0x18DAF100, can_data, true, false);

    EXPECT_GE(packet.size(), 12 + 2 + 8);
    EXPECT_EQ(packet[0], 0x7F);  // ACF subtype
}

// Test CAN FD DLC encoding values
TEST_F(AVTPCanReaderTest, CanFdDlcEncoding) {
    // CAN FD DLC encoding table
    // DLC 0-8: data length = DLC
    // DLC 9: 12 bytes
    // DLC 10: 16 bytes
    // DLC 11: 20 bytes
    // DLC 12: 24 bytes
    // DLC 13: 32 bytes
    // DLC 14: 48 bytes
    // DLC 15: 64 bytes

    static const size_t dlc_to_len[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

    EXPECT_EQ(dlc_to_len[9], 12);
    EXPECT_EQ(dlc_to_len[12], 24);
    EXPECT_EQ(dlc_to_len[15], 64);
}

// Test close when not open
TEST_F(AVTPCanReaderTest, CloseWhenNotOpen) {
    EXPECT_FALSE(reader.is_open());
    reader.close();  // Should not crash
    EXPECT_FALSE(reader.is_open());
}

// Test double open protection
TEST_F(AVTPCanReaderTest, DoubleOpenProtection) {
    // First open will likely fail due to permissions
    bool first = reader.open("eth0");
    // Second open should also fail (either still not permitted, or already open warning)
    bool second = reader.open("eth0");

    // Both likely false on CI, but the second should definitely not succeed
    // if first succeeded
    if (first) {
        EXPECT_FALSE(second);
        reader.close();
    }
}
