#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <dbcppp/Network.h>

namespace can_to_vss {

class DBCParser {
public:
    // Map of enum description to integer value
    using EnumMap = std::unordered_map<std::string, int64_t>;
    
    explicit DBCParser(const std::string& dbc_file);
    ~DBCParser() = default;

    bool parse();
    
    std::vector<std::pair<std::string, double>> decode_message(uint32_t can_id, const uint8_t* data, size_t length) const;
    
    bool has_message(uint32_t can_id) const;
    std::vector<std::string> get_signal_names(uint32_t can_id) const;
    
    // Get enum mappings for a signal (returns empty map if no enums defined)
    EnumMap get_signal_enums(const std::string& signal_name) const;
    
    // Get all signals with their enum mappings
    std::unordered_map<std::string, EnumMap> get_all_signal_enums() const;

private:
    std::string dbc_file_;
    std::unique_ptr<dbcppp::INetwork> network_;
    
    // Cache of signal name to enum mappings
    std::unordered_map<std::string, EnumMap> signal_enums_;
};

} // namespace can_to_vss