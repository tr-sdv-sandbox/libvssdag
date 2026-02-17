#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
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

    /// @brief Construct allowing injection of CANReader
    /// @param can_reader Type implementing CANReader
    /// @param dbc_file_path Path to DBC file
    /// @param mappings Signal mappings from YAML
    CANSignalSource(std::unique_ptr<CANReader> can_reader,
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
    
    // Map from CAN message ID to (DBC signal name → VSS signal name)
    // Outer key: CAN ID for fast frame filtering
    // Inner key: DBC signal name within that message
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::string>> can_id_signal_map_;
    
    // Reader thread
    std::unique_ptr<std::thread> reader_thread_;
    std::atomic<bool> running_{false};

    // Callback for CAN frames
    void handle_can_frame(const CANFrame& frame);
};

} // namespace vssdag