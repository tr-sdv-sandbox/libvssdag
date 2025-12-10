/// @file avtp_test_sender/main.cpp
/// @brief IEEE 1722 AVTP CAN test frame sender
///
/// Sends test CAN frames over IEEE 1722 AVTP for testing AVTPCanReader.
/// Uses Open1722 library for packet construction.
///
/// Usage:
///   avtp_test_sender --interface eth0 --can-id 0x123 --data "01 02 03 04"
///   avtp_test_sender --interface eth0 --count 100 --interval 10

#include "vssdag/can/avtp_can_sender.h"
#include <glog/logging.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG(INFO) << "Received signal " << signal << ", stopping...";
        g_running = false;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "\nOptions:\n"
              << "  --interface NAME   Ethernet interface (default: eth0)\n"
              << "  --can-id ID        CAN ID in hex (default: 0x123)\n"
              << "  --data \"XX XX\"     CAN data bytes in hex (default: 01 02 03 04 05 06 07 08)\n"
              << "  --extended         Use extended (29-bit) CAN ID\n"
              << "  --count N          Number of frames to send (default: 1, 0=infinite)\n"
              << "  --interval MS      Interval between frames in ms (default: 100)\n"
              << "  --stream-id HEX    8-byte stream ID in hex (default: 0011223344556677)\n"
              << "  --dst-mac MAC      Destination MAC (default: 01:80:c2:00:00:0e)\n"
              << "  --help             Show this help\n"
              << "\nExamples:\n"
              << "  " << program_name << " --interface eth0 --can-id 0x7DF --data \"02 01 00\"\n"
              << "  " << program_name << " --interface enp0s3 --count 100 --interval 10\n"
              << "  " << program_name << " --interface eth0 --extended --can-id 0x18DAF100\n";
}

std::vector<uint8_t> parse_hex_string(const std::string& hex_str) {
    std::vector<uint8_t> result;
    std::istringstream iss(hex_str);
    std::string byte_str;

    while (iss >> byte_str) {
        unsigned int byte_val;
        std::istringstream byte_iss(byte_str);
        byte_iss >> std::hex >> byte_val;
        result.push_back(static_cast<uint8_t>(byte_val));
    }

    return result;
}

bool parse_mac(const std::string& mac_str, uint8_t mac[6]) {
    unsigned int bytes[6];
    if (sscanf(mac_str.c_str(), "%x:%x:%x:%x:%x:%x",
               &bytes[0], &bytes[1], &bytes[2],
               &bytes[3], &bytes[4], &bytes[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        mac[i] = static_cast<uint8_t>(bytes[i]);
    }
    return true;
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;

    // Default values
    std::string interface = "eth0";
    uint32_t can_id = 0x123;
    std::vector<uint8_t> can_data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    bool extended_id = false;
    int count = 1;
    int interval_ms = 100;
    uint8_t stream_id[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t dst_mac[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--interface" && i + 1 < argc) {
            interface = argv[++i];
        } else if (arg == "--can-id" && i + 1 < argc) {
            can_id = std::stoul(argv[++i], nullptr, 16);
        } else if (arg == "--data" && i + 1 < argc) {
            can_data = parse_hex_string(argv[++i]);
        } else if (arg == "--extended") {
            extended_id = true;
        } else if (arg == "--count" && i + 1 < argc) {
            count = std::stoi(argv[++i]);
        } else if (arg == "--interval" && i + 1 < argc) {
            interval_ms = std::stoi(argv[++i]);
        } else if (arg == "--stream-id" && i + 1 < argc) {
            std::string sid_str = argv[++i];
            if (sid_str.length() == 16) {
                for (int j = 0; j < 8; j++) {
                    stream_id[j] = static_cast<uint8_t>(
                        std::stoul(sid_str.substr(j * 2, 2), nullptr, 16));
                }
            }
        } else if (arg == "--dst-mac" && i + 1 < argc) {
            if (!parse_mac(argv[++i], dst_mac)) {
                LOG(ERROR) << "Invalid MAC address format";
                return 1;
            }
        } else {
            LOG(ERROR) << "Unknown argument: " << arg;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Configure sender
    vssdag::AVTPCanSender::Config config;
    std::memcpy(config.stream_id, stream_id, 8);
    std::memcpy(config.dst_mac, dst_mac, 6);

    vssdag::AVTPCanSender sender(config);

    LOG(INFO) << "Opening AVTP sender on " << interface;
    if (!sender.open(interface)) {
        LOG(ERROR) << "Failed to open sender on " << interface;
        LOG(ERROR) << "Hint: Run with sudo or grant CAP_NET_RAW capability";
        return 1;
    }

    // Build CAN frame
    vssdag::CANFrame frame;
    frame.id = can_id;
    if (extended_id) {
        frame.id |= 0x80000000;  // Extended ID flag
    }
    frame.data = can_data;

    LOG(INFO) << "Sending CAN frames:";
    LOG(INFO) << "  Interface: " << interface;
    LOG(INFO) << "  CAN ID: 0x" << std::hex << can_id
              << (extended_id ? " (extended)" : " (standard)") << std::dec;
    LOG(INFO) << "  Data length: " << can_data.size() << " bytes";
    LOG(INFO) << "  Count: " << (count == 0 ? "infinite" : std::to_string(count));
    LOG(INFO) << "  Interval: " << interval_ms << " ms";

    int sent = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (g_running && (count == 0 || sent < count)) {
        if (sender.send(frame)) {
            sent++;
            VLOG(1) << "Sent frame " << sent;
        } else {
            LOG(WARNING) << "Failed to send frame " << sent + 1;
        }

        if (count == 0 || sent < count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    sender.close();

    auto stats = sender.get_stats();
    LOG(INFO) << "Done. Sent " << sent << " frames in " << duration << " ms";
    LOG(INFO) << "Stats: packets=" << stats.packets_sent
              << ", frames=" << stats.can_frames_sent
              << ", errors=" << stats.send_errors;

    return 0;
}
