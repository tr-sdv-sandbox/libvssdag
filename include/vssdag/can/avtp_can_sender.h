#pragma once

#include "vssdag/can/can_reader.h"  // For CANFrame
#include <string>
#include <cstdint>
#include <vector>

namespace vssdag {

/// @brief IEEE 1722 AVTP CAN sender using Open1722
///
/// Sends CAN frames encapsulated in IEEE 1722 AVTP packets over raw Ethernet.
/// Uses Open1722 library for AVTP packet construction to ensure compatibility
/// with AVTPCanReader and other IEEE 1722 implementations.
///
/// Supports NTSCF (Non-Time-Synchronous Control Format) with ACF CAN messages.
///
/// Usage:
///   AVTPCanSender sender;
///   sender.open("eth0");
///   sender.send(frame);  // Send single CAN frame
///   sender.close();
///
class AVTPCanSender {
public:
    /// @brief AVTP stream configuration
    struct Config {
        uint8_t stream_id[8] = {0};     ///< AVTP stream ID
        uint8_t src_mac[6] = {0};       ///< Source MAC address (0 = use interface MAC)
        uint8_t dst_mac[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};  ///< Default: AVTP multicast
        uint16_t ethertype = 0x22F0;    ///< IEEE 1722 ethertype
        uint8_t can_bus_id = 0;         ///< CAN bus identifier
    };

    AVTPCanSender();
    explicit AVTPCanSender(const Config& config);
    ~AVTPCanSender();

    // Non-copyable
    AVTPCanSender(const AVTPCanSender&) = delete;
    AVTPCanSender& operator=(const AVTPCanSender&) = delete;

    /// @brief Open raw socket on Ethernet interface
    /// @param interface Ethernet interface name (e.g., "eth0")
    /// @return true on success
    bool open(const std::string& interface);

    void close();
    bool is_open() const;

    /// @brief Send a single CAN frame as AVTP packet
    /// @param frame CAN frame to send
    /// @return true on success
    bool send(const CANFrame& frame);

    /// @brief Send multiple CAN frames in a single AVTP packet
    /// @param frames Vector of CAN frames to send
    /// @return Number of frames successfully sent
    int send_batch(const std::vector<CANFrame>& frames);

    /// @brief Get statistics
    struct Stats {
        uint64_t packets_sent = 0;
        uint64_t can_frames_sent = 0;
        uint64_t send_errors = 0;
        uint8_t sequence_num = 0;
    };
    Stats get_stats() const { return stats_; }

    /// @brief Set stream ID
    void set_stream_id(const uint8_t stream_id[8]);

    /// @brief Set destination MAC address
    void set_dst_mac(const uint8_t mac[6]);

private:
    Config config_;
    int socket_fd_ = -1;
    int if_index_ = 0;
    std::string interface_name_;
    Stats stats_;

    /// @brief Build complete Ethernet frame with AVTP NTSCF + ACF CAN
    /// @param frames CAN frames to encapsulate
    /// @param buffer Output buffer (must be large enough for Ethernet frame)
    /// @param buffer_size Size of output buffer
    /// @return Size of complete Ethernet frame, or 0 on error
    size_t build_ethernet_frame(const std::vector<CANFrame>& frames,
                                uint8_t* buffer, size_t buffer_size);
};

} // namespace vssdag
