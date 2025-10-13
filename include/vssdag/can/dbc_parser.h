#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include <dbcppp/Network.h>
#include "vssdag/can/dbc_types.h"

namespace vssdag {

class DBCParser {
public:
    // Map of enum description to integer value
    using EnumMap = std::unordered_map<std::string, int64_t>;
    
    explicit DBCParser(const std::string& dbc_file);
    ~DBCParser() = default;

    bool parse();
    
    // Decode message with type information preserved
    std::unordered_map<std::string, DBCDecodedValue> decode_message(uint32_t can_id, const uint8_t* data, size_t length) const;
    
    // Decode message and return as vector of signal updates
    std::vector<DBCSignalUpdate> decode_message_as_updates(uint32_t can_id, const uint8_t* data, size_t length) const;
    
    bool has_message(uint32_t can_id) const;
    std::vector<std::string> get_signal_names(uint32_t can_id) const;
    
    // Get enum mappings for a signal (returns empty map if no enums defined)
    EnumMap get_signal_enums(const std::string& signal_name) const;
    
    // Get all signals with their enum mappings
    std::unordered_map<std::string, EnumMap> get_all_signal_enums() const;
    
    // Get the CAN message ID that contains a specific signal
    std::optional<uint32_t> get_message_id_for_signal(const std::string& signal_name) const;
    
    // Convert an enum value to its string representation (returns empty optional if not found)
    std::optional<std::string> get_enum_string(const std::string& signal_name, int64_t value) const;

private:
    std::string dbc_file_;
    std::unique_ptr<dbcppp::INetwork> network_;
    
    // Complete signal information including pre-calculated invalid/NA patterns
    struct SignalInfo {
        // Enum mappings (if any)
        EnumMap enums;  // string -> int64_t
        std::unordered_map<int64_t, std::string> reverse_enums;  // int64_t -> string for fast lookup
        
        // Pre-calculated invalid/NA detection values
        uint64_t invalid_raw_value;     // All bits set pattern
        uint64_t na_raw_value;          // All bits minus one pattern
        bool can_use_invalid_pattern;   // Is invalid pattern outside valid range?
        bool can_use_na_pattern;        // Is NA pattern outside valid range?
        double min_physical;            // Min valid physical value from DBC
        double max_physical;            // Max valid physical value from DBC
        
        // Quick inline status check
        vss::types::SignalQuality check_status(uint64_t raw_value, double physical_value) const {
            // Check for invalid pattern
            if (can_use_invalid_pattern && raw_value == invalid_raw_value) {
                return vss::types::SignalQuality::INVALID;
            }
            // Check for NA pattern
            if (can_use_na_pattern && raw_value == na_raw_value) {
                return vss::types::SignalQuality::NOT_AVAILABLE;
            }
            // Check if physical value is out of range
            if (physical_value < min_physical || physical_value > max_physical) {
                return vss::types::SignalQuality::INVALID;
            }
            return vss::types::SignalQuality::VALID;
        }
    };
    
    // Single cache for all signal information
    std::unordered_map<std::string, SignalInfo> signal_info_;
};

} // namespace vssdag