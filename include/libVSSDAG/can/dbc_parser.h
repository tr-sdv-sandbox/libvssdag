#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include <dbcppp/Network.h>
#include "dbc_types.h"

namespace can_to_vss {

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

private:
    std::string dbc_file_;
    std::unique_ptr<dbcppp::INetwork> network_;
    
    // Cache of signal name to enum mappings
    std::unordered_map<std::string, EnumMap> signal_enums_;
};

} // namespace can_to_vss