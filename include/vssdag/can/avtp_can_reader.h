#pragma once

#include "vssdag/can/can_reader.h"
#include <string>
#include <cstdint>

namespace vssdag {

/// @brief IEEE 1722 AVTP CAN reader
///
/// Receives CAN frames encapsulated in IEEE 1722 AVTP packets over raw Ethernet.
/// Supports AVTP CAN Brief format (AVTP subtype 0x7F, ACF type 0x02).
///
/// Usage:
///   AVTPCanReader reader;
///   reader.set_frame_handler([](const CANFrame& f) { ... });
///   reader.open("eth0");
///   reader.read_loop();  // Blocking, call from thread
///
class AVTPCanReader : public CANReader {
public:
    /// @brief AVTP stream configuration
    struct Config {
        uint8_t stream_id[8] = {0};     ///< AVTP stream ID to filter (0 = accept all)
        bool filter_stream_id = false;   ///< Enable stream ID filtering
        uint16_t ethertype = 0x22F0;     ///< IEEE 1722 ethertype
    };

    AVTPCanReader();
    explicit AVTPCanReader(const Config& config);
    ~AVTPCanReader() override;

    /// @brief Open raw socket on Ethernet interface
    /// @param interface Ethernet interface name (e.g., "eth0")
    /// @return true on success
    bool open(const std::string& interface) override;

    void close() override;
    bool is_open() const override;

    /// @brief Blocking read loop - receives AVTP packets, extracts CAN frames
    /// Calls frame_handler_ for each CAN frame extracted from AVTP packets
    void read_loop() override;

    void stop() override;

    /// @brief Get statistics
    struct Stats {
        uint64_t packets_received = 0;
        uint64_t can_frames_extracted = 0;
        uint64_t invalid_packets = 0;
        uint64_t filtered_packets = 0;
    };
    Stats get_stats() const { return stats_; }

private:
    Config config_;
    int socket_fd_ = -1;
    bool should_stop_ = false;
    std::string interface_name_;
    Stats stats_;

    /// @brief Parse AVTP packet and extract CAN frames
    /// @param data Raw Ethernet frame data (after ethertype)
    /// @param len Length of data
    /// @return Number of CAN frames extracted and delivered to handler
    int parse_avtp_packet(const uint8_t* data, size_t len);

    /// @brief Parse ACF CAN message from AVTP payload
    /// @param data ACF message data
    /// @param len Length of data
    /// @param frame Output CAN frame
    /// @return true if valid CAN frame extracted
    bool parse_acf_can(const uint8_t* data, size_t len, CANFrame& frame);
};

} // namespace vssdag
