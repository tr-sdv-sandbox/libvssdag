/// @file avtp_canplayer/main.cpp
/// @brief IEEE 1722 AVTP CAN log replay tool
///
/// Replays candump log files over IEEE 1722 AVTP, similar to canplayer
/// but sends frames over Ethernet instead of SocketCAN.
///
/// Supports standard candump format:
///   (timestamp) interface CAN_ID#DATA_HEX
///
/// Supports both 11-bit standard and 29-bit extended CAN IDs.
///
/// Usage:
///   avtp_canplayer -I candump.log --interface eth0
///   avtp_canplayer -I candump.log --interface eth0 --speed 2.0

#include "vssdag/can/avtp_can_sender.h"
#include <glog/logging.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <regex>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG(INFO) << "Received signal " << signal << ", stopping...";
        g_running = false;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] -I <logfile>\n"
              << "\nOptions:\n"
              << "  -I, --input FILE     Input candump log file (required)\n"
              << "  --interface NAME     Ethernet interface (default: eth0)\n"
              << "  --speed FACTOR       Playback speed multiplier (default: 1.0)\n"
              << "  --loop               Loop playback continuously\n"
              << "  --no-timestamps      Ignore timestamps, send as fast as possible\n"
              << "  --interval MS        Fixed interval between frames in ms (overrides timestamps)\n"
              << "  --stream-id HEX      8-byte stream ID in hex (default: 0011223344556677)\n"
              << "  --dst-mac MAC        Destination MAC (default: 01:80:c2:00:00:0e)\n"
              << "  --verbose            Verbose output\n"
              << "  --help               Show this help\n"
              << "\nExamples:\n"
              << "  " << program_name << " -I candump.log --interface eth0\n"
              << "  " << program_name << " -I candump.log --interface enp0s3 --speed 2.0\n"
              << "  " << program_name << " -I candump.log --interface eth0 --loop\n"
              << "  " << program_name << " -I candump.log --interface eth0 --no-timestamps\n";
}

struct CanLogEntry {
    double timestamp;
    std::string interface;
    uint32_t can_id;
    bool extended_id;
    std::vector<uint8_t> data;
};

// Parse a candump log line
// Format: (timestamp) interface CAN_ID#DATA_HEX
bool parse_candump_line(const std::string& line, CanLogEntry& entry) {
    // Skip empty lines and comments
    if (line.empty() || line[0] == '#') {
        return false;
    }

    // Regex for candump format: (timestamp) interface CAN_ID#DATA
    static std::regex candump_regex(R"(\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*))");
    std::smatch match;

    if (!std::regex_match(line, match, candump_regex)) {
        return false;
    }

    entry.timestamp = std::stod(match[1].str());
    entry.interface = match[2].str();

    // Parse CAN ID - if > 0x7FF, it's extended
    std::string can_id_str = match[3].str();
    entry.can_id = std::stoul(can_id_str, nullptr, 16);
    entry.extended_id = (entry.can_id > 0x7FF);

    // Parse data bytes
    std::string data_str = match[4].str();
    entry.data.clear();
    for (size_t i = 0; i + 1 < data_str.length(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(
            std::stoul(data_str.substr(i, 2), nullptr, 16));
        entry.data.push_back(byte);
    }

    return true;
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
    std::string input_file;
    std::string interface = "eth0";
    double speed = 1.0;
    bool loop = false;
    bool use_timestamps = true;
    int fixed_interval_ms = 0;
    uint8_t stream_id[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t dst_mac[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
    bool verbose = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-I" || arg == "--input") && i + 1 < argc) {
            input_file = argv[++i];
        } else if (arg == "--interface" && i + 1 < argc) {
            interface = argv[++i];
        } else if (arg == "--speed" && i + 1 < argc) {
            speed = std::stod(argv[++i]);
        } else if (arg == "--loop") {
            loop = true;
        } else if (arg == "--no-timestamps") {
            use_timestamps = false;
        } else if (arg == "--interval" && i + 1 < argc) {
            fixed_interval_ms = std::stoi(argv[++i]);
            use_timestamps = false;
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
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg[0] != '-') {
            // Positional argument - treat as input file
            input_file = arg;
        } else {
            LOG(ERROR) << "Unknown argument: " << arg;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (input_file.empty()) {
        LOG(ERROR) << "Input file required";
        print_usage(argv[0]);
        return 1;
    }

    // Load log file
    std::vector<CanLogEntry> entries;
    std::ifstream file(input_file);
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to open input file: " << input_file;
        return 1;
    }

    std::string line;
    size_t line_num = 0;
    size_t parsed = 0;
    while (std::getline(file, line)) {
        line_num++;
        CanLogEntry entry;
        if (parse_candump_line(line, entry)) {
            entries.push_back(entry);
            parsed++;
        }
    }
    file.close();

    if (entries.empty()) {
        LOG(ERROR) << "No valid CAN frames found in " << input_file;
        return 1;
    }

    LOG(INFO) << "Loaded " << parsed << " CAN frames from " << input_file;

    // Count extended vs standard IDs
    size_t extended_count = 0;
    for (const auto& e : entries) {
        if (e.extended_id) extended_count++;
    }
    LOG(INFO) << "  Standard IDs: " << (entries.size() - extended_count)
              << ", Extended IDs: " << extended_count;

    // Calculate log duration
    if (entries.size() > 1) {
        double duration = entries.back().timestamp - entries.front().timestamp;
        LOG(INFO) << "  Log duration: " << std::fixed << std::setprecision(2)
                  << duration << " seconds";
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

    LOG(INFO) << "Starting playback...";
    if (use_timestamps) {
        LOG(INFO) << "  Speed: " << speed << "x";
    } else if (fixed_interval_ms > 0) {
        LOG(INFO) << "  Fixed interval: " << fixed_interval_ms << " ms";
    } else {
        LOG(INFO) << "  No timing (as fast as possible)";
    }
    if (loop) {
        LOG(INFO) << "  Looping enabled";
    }

    auto start_time = std::chrono::steady_clock::now();
    uint64_t total_frames = 0;
    uint64_t loop_count = 0;

    do {
        loop_count++;
        if (loop && loop_count > 1) {
            LOG(INFO) << "Starting loop " << loop_count;
        }

        double base_timestamp = entries.front().timestamp;
        auto playback_start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < entries.size() && g_running; ++i) {
            const auto& entry = entries[i];

            // Handle timing
            if (use_timestamps && i > 0) {
                double delta = (entry.timestamp - base_timestamp) / speed;
                auto target_time = playback_start +
                    std::chrono::duration<double>(delta);

                auto now = std::chrono::steady_clock::now();
                if (target_time > now) {
                    std::this_thread::sleep_until(target_time);
                }
            } else if (fixed_interval_ms > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(fixed_interval_ms));
            }

            // Build CAN frame
            vssdag::CANFrame frame;
            frame.id = entry.can_id;
            if (entry.extended_id) {
                frame.id |= 0x80000000;  // Set extended ID flag
            }
            frame.data = entry.data;

            // Send
            if (sender.send(frame)) {
                total_frames++;
                if (verbose) {
                    std::cout << "TX: " << std::hex << std::setw(8) << std::setfill('0')
                              << entry.can_id
                              << (entry.extended_id ? " (ext)" : "")
                              << " [" << std::dec << entry.data.size() << "] ";
                    for (uint8_t b : entry.data) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0')
                                  << static_cast<int>(b) << " ";
                    }
                    std::cout << std::dec << "\n";
                }
            } else {
                LOG(WARNING) << "Failed to send frame " << i;
            }

            // Progress every 1000 frames
            if (!verbose && total_frames % 1000 == 0) {
                std::cout << "\rSent " << total_frames << " frames..." << std::flush;
            }
        }
    } while (loop && g_running);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    std::cout << "\n";
    sender.close();

    auto stats = sender.get_stats();
    LOG(INFO) << "Playback complete.";
    LOG(INFO) << "  Total frames sent: " << total_frames;
    LOG(INFO) << "  Packets sent: " << stats.packets_sent;
    LOG(INFO) << "  Duration: " << duration << " ms";
    if (duration > 0) {
        LOG(INFO) << "  Rate: " << (total_frames * 1000 / duration) << " frames/sec";
    }

    return 0;
}
