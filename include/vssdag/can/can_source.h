#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <moodycamel/concurrentqueue.h>
#include "vssdag/signal_source.h"
#include "vssdag/can/can_reader.h"
#include "vssdag/can/dbc_parser.h"
#include "vssdag/mapping_types.h"

namespace vssdag {

/// @brief CAN transport type
enum class CANTransport {
    SOCKETCAN,  ///< Linux SocketCAN (vcan, can0, etc.)
    AVTP        ///< IEEE 1722 AVTP over Ethernet
};

class CANSignalSource : public ISignalSource {
public:
    /// @brief Construct with specified transport type
    /// @param transport Transport type (SOCKETCAN or AVTP)
    /// @param interface_name Interface name (CAN interface or Ethernet interface)
    /// @param dbc_file_path Path to DBC file
    /// @param mappings Signal mappings from YAML
    CANSignalSource(CANTransport transport,
                    const std::string& interface_name,
                    const std::string& dbc_file_path,
                    const std::unordered_map<std::string, SignalMapping>& mappings);

    ~CANSignalSource() override;
    
    bool initialize() override;
    
    std::vector<SignalUpdate> poll() override;
    
    std::vector<std::string> get_exported_signals() const override;
    
    // Stop the reader thread
    void stop();
    
private:
    CANTransport transport_;
    std::string interface_name_;
    std::string dbc_file_path_;

    std::unique_ptr<CANReader> can_reader_;
    std::unique_ptr<DBCParser> dbc_parser_;
    
    // Lock-free queue for signal updates
    moodycamel::ConcurrentQueue<SignalUpdate> signal_queue_;
    
    // Mappings from YAML
    std::unordered_map<std::string, SignalMapping> mappings_;
    
    // DBC signal names we need (extracted from mappings where source.type == "dbc")
    std::vector<std::pair<std::string, std::string>> dbc_signal_names_;
    
    // Map from DBC message and signal name to our signal name
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dbc_to_signal_name_;
    
    // CAN message IDs we need to process (derived from dbc_signal_names via DBC)
    std::unordered_set<uint32_t> required_can_ids_;
    
    // Reader thread
    std::unique_ptr<std::thread> reader_thread_;
    std::atomic<bool> running_{false};
    
    // Callback for CAN frames
    void handle_can_frame(const CANFrame& frame);
};

} // namespace vssdag