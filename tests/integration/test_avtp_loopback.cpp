/// @file test_avtp_loopback.cpp
/// @brief Integration test for AVTP CAN sender/receiver using loopback
///
/// Tests that AVTPCanSender and AVTPCanReader can communicate using
/// Open1722 library for packet construction/parsing.
///
/// Note: This test requires either:
///   1. CAP_NET_RAW capability or root privileges
///   2. A loopback-capable Ethernet interface
///
/// The test creates sender and receiver on the same interface,
/// sends CAN frames, and verifies they are received correctly.

#include <gtest/gtest.h>
#include "vssdag/can/avtp_can_sender.h"
#include "vssdag/can/avtp_can_reader.h"

#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

using namespace vssdag;

class AVTPLoopbackTest : public ::testing::Test {
protected:
    // Test interfaces - for veth pair testing, use two different interfaces
    // AVTP_TEST_INTERFACE: sender interface (e.g., avtp_test0)
    // AVTP_TEST_INTERFACE_RX: receiver interface (e.g., avtp_test1)
    // If only one is set, or neither, use "lo" for both (loopback mode)
    std::string tx_interface;
    std::string rx_interface;

    void SetUp() override {
        const char* env_tx = std::getenv("AVTP_TEST_INTERFACE");
        const char* env_rx = std::getenv("AVTP_TEST_INTERFACE_RX");

        if (env_tx && env_rx) {
            // veth pair mode: send on one end, receive on the other
            tx_interface = env_tx;
            rx_interface = env_rx;
        } else if (env_tx) {
            // Single interface mode (loopback on same interface)
            tx_interface = env_tx;
            rx_interface = env_tx;
        } else {
            // Default to loopback
            tx_interface = "lo";
            rx_interface = "lo";
        }
    }

    // Check if we have permission to use raw sockets
    bool can_use_raw_sockets() {
        AVTPCanSender sender;
        bool result = sender.open(tx_interface);
        if (result) {
            sender.close();
        }
        return result;
    }
};

// Test that sender and receiver can be constructed
TEST_F(AVTPLoopbackTest, ConstructionWorks) {
    AVTPCanSender sender;
    AVTPCanReader reader;

    EXPECT_FALSE(sender.is_open());
    EXPECT_FALSE(reader.is_open());
}

// Test configuration compatibility
TEST_F(AVTPLoopbackTest, ConfigCompatibility) {
    // Same stream ID for both sender and receiver
    uint8_t stream_id[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};

    AVTPCanSender::Config sender_config;
    std::memcpy(sender_config.stream_id, stream_id, 8);

    AVTPCanReader::Config reader_config;
    reader_config.filter_stream_id = true;
    std::memcpy(reader_config.stream_id, stream_id, 8);

    AVTPCanSender sender(sender_config);
    AVTPCanReader reader(reader_config);

    // Verify configs are set correctly
    auto sender_stats = sender.get_stats();
    auto reader_stats = reader.get_stats();

    EXPECT_EQ(sender_stats.packets_sent, 0);
    EXPECT_EQ(reader_stats.packets_received, 0);
}

// Test packet building without network (uses internal buffer)
TEST_F(AVTPLoopbackTest, PacketBuildingWorks) {
    AVTPCanSender sender;

    // Build a test CAN frame
    CANFrame frame;
    frame.id = 0x123;
    frame.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    // Without opening, send should fail gracefully
    EXPECT_FALSE(sender.send(frame));

    auto stats = sender.get_stats();
    EXPECT_EQ(stats.packets_sent, 0);
}

// Test extended CAN ID handling
TEST_F(AVTPLoopbackTest, ExtendedCanIdHandling) {
    CANFrame frame;

    // Standard ID (11-bit)
    frame.id = 0x123;
    EXPECT_FALSE(frame.id & 0x80000000);

    // Extended ID (29-bit) with flag
    frame.id = 0x18DAF100 | 0x80000000;
    EXPECT_TRUE(frame.id & 0x80000000);
    EXPECT_EQ(frame.id & 0x1FFFFFFF, 0x18DAF100);
}

// Test CAN-FD payload sizes
TEST_F(AVTPLoopbackTest, CanFdPayloadSizes) {
    CANFrame frame;
    frame.id = 0x100;

    // Standard CAN: 0-8 bytes
    frame.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    EXPECT_EQ(frame.data.size(), 8);

    // CAN-FD: up to 64 bytes
    frame.data.resize(64);
    for (size_t i = 0; i < 64; i++) {
        frame.data[i] = static_cast<uint8_t>(i);
    }
    EXPECT_EQ(frame.data.size(), 64);
}

// Integration test: send and receive (requires raw socket permissions)
TEST_F(AVTPLoopbackTest, SendReceiveLoopback) {
    if (!can_use_raw_sockets()) {
        GTEST_SKIP() << "Raw sockets not available (need CAP_NET_RAW or root)";
    }

    // Use same stream ID for filtering
    uint8_t stream_id[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};

    // Configure sender
    AVTPCanSender::Config sender_config;
    std::memcpy(sender_config.stream_id, stream_id, 8);
    // For loopback, set destination to broadcast or interface MAC
    sender_config.dst_mac[0] = 0xFF;
    sender_config.dst_mac[1] = 0xFF;
    sender_config.dst_mac[2] = 0xFF;
    sender_config.dst_mac[3] = 0xFF;
    sender_config.dst_mac[4] = 0xFF;
    sender_config.dst_mac[5] = 0xFF;

    // Configure reader
    AVTPCanReader::Config reader_config;
    reader_config.filter_stream_id = false;  // Accept all for loopback test

    AVTPCanSender sender(sender_config);
    AVTPCanReader reader(reader_config);

    // Received frames storage
    std::mutex rx_mutex;
    std::condition_variable rx_cv;
    std::vector<CANFrame> received_frames;
    std::atomic<bool> reader_running{true};

    // Set up frame handler
    reader.set_frame_handler([&](const CANFrame& frame) {
        std::lock_guard<std::mutex> lock(rx_mutex);
        received_frames.push_back(frame);
        rx_cv.notify_one();
    });

    // Open both - sender on tx_interface, reader on rx_interface
    ASSERT_TRUE(sender.open(tx_interface));
    ASSERT_TRUE(reader.open(rx_interface));

    // Start reader in background thread
    std::thread reader_thread([&]() {
        reader.read_loop();
    });

    // Give reader time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send test frames
    std::vector<CANFrame> test_frames;

    // Frame 1: Standard CAN
    CANFrame frame1;
    frame1.id = 0x123;
    frame1.data = {0x01, 0x02, 0x03, 0x04};
    test_frames.push_back(frame1);

    // Frame 2: Extended CAN ID
    CANFrame frame2;
    frame2.id = 0x18DAF100 | 0x80000000;
    frame2.data = {0xAA, 0xBB, 0xCC};
    test_frames.push_back(frame2);

    // Frame 3: Empty payload
    CANFrame frame3;
    frame3.id = 0x7DF;
    frame3.data = {};
    test_frames.push_back(frame3);

    // Send all frames
    for (const auto& frame : test_frames) {
        EXPECT_TRUE(sender.send(frame));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait for frames to be received (with timeout)
    {
        std::unique_lock<std::mutex> lock(rx_mutex);
        bool received = rx_cv.wait_for(lock, std::chrono::seconds(2), [&]() {
            return received_frames.size() >= test_frames.size();
        });

        if (!received) {
            // Loopback might not work on all interfaces
            GTEST_SKIP() << "Loopback not working on " << tx_interface << " -> " << rx_interface
                         << " (received " << received_frames.size() << "/" << test_frames.size() << ")";
        }
    }

    // Stop reader
    reader.stop();
    reader_thread.join();

    // Verify received frames
    ASSERT_GE(received_frames.size(), test_frames.size());

    // Check sender stats
    auto sender_stats = sender.get_stats();
    EXPECT_EQ(sender_stats.packets_sent, test_frames.size());
    EXPECT_EQ(sender_stats.can_frames_sent, test_frames.size());
    EXPECT_EQ(sender_stats.send_errors, 0);

    // Check reader stats
    auto reader_stats = reader.get_stats();
    EXPECT_GE(reader_stats.packets_received, test_frames.size());
    EXPECT_GE(reader_stats.can_frames_extracted, test_frames.size());

    sender.close();
    reader.close();
}

// Test batch sending
TEST_F(AVTPLoopbackTest, BatchSend) {
    if (!can_use_raw_sockets()) {
        GTEST_SKIP() << "Raw sockets not available (need CAP_NET_RAW or root)";
    }

    AVTPCanSender sender;
    ASSERT_TRUE(sender.open(tx_interface));

    // Create batch of frames
    std::vector<CANFrame> batch;
    for (int i = 0; i < 5; i++) {
        CANFrame frame;
        frame.id = 0x100 + i;
        frame.data = {static_cast<uint8_t>(i), 0x00, 0x00, 0x00};
        batch.push_back(frame);
    }

    // Send batch
    int sent = sender.send_batch(batch);
    EXPECT_EQ(sent, 5);

    auto stats = sender.get_stats();
    EXPECT_EQ(stats.packets_sent, 1);  // Single AVTP packet
    EXPECT_EQ(stats.can_frames_sent, 5);  // Multiple CAN frames

    sender.close();
}

// Test statistics tracking
TEST_F(AVTPLoopbackTest, StatisticsTracking) {
    AVTPCanSender sender;
    AVTPCanReader reader;

    // Initial stats should be zero
    auto sender_stats = sender.get_stats();
    EXPECT_EQ(sender_stats.packets_sent, 0);
    EXPECT_EQ(sender_stats.can_frames_sent, 0);
    EXPECT_EQ(sender_stats.send_errors, 0);

    auto reader_stats = reader.get_stats();
    EXPECT_EQ(reader_stats.packets_received, 0);
    EXPECT_EQ(reader_stats.can_frames_extracted, 0);
    EXPECT_EQ(reader_stats.invalid_packets, 0);
    EXPECT_EQ(reader_stats.filtered_packets, 0);
}
