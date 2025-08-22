#include "libVSSDAG/can/dbc_parser.h"
#include <dbcppp/Network.h>
#include <glog/logging.h>
#include <fstream>
#include <cmath>
#include <limits>

namespace can_to_vss {

DBCParser::DBCParser(const std::string& dbc_file)
    : dbc_file_(dbc_file) {
}

bool DBCParser::parse() {
    std::ifstream file(dbc_file_);
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to open DBC file: " << dbc_file_;
        return false;
    }

    try {
        network_ = dbcppp::INetwork::LoadDBCFromIs(file);
        if (!network_) {
            LOG(ERROR) << "Failed to parse DBC file: " << dbc_file_;
            return false;
        }
        
        // Extract value descriptions (enums) for all signals
        signal_enums_.clear();
        for (const auto& msg : network_->Messages()) {
            for (const auto& sig : msg.Signals()) {
                EnumMap enum_map;
                for (const auto& value_desc : sig.ValueEncodingDescriptions()) {
                    enum_map[value_desc.Description()] = value_desc.Value();
                    VLOG(2) << "Signal " << sig.Name() << " enum: " 
                            << value_desc.Value() << " = " << value_desc.Description();
                }
                if (!enum_map.empty()) {
                    signal_enums_[sig.Name()] = std::move(enum_map);
                }
            }
        }
        
        LOG(INFO) << "Successfully parsed DBC file: " << dbc_file_ 
                  << " with " << signal_enums_.size() << " signals having enums";
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception parsing DBC file: " << e.what();
        return false;
    }
}

std::unordered_map<std::string, DBCDecodedValue> DBCParser::decode_message(uint32_t can_id, const uint8_t* data, size_t length) const {
    std::unordered_map<std::string, DBCDecodedValue> decoded_signals;
    
    if (!network_) {
        LOG(ERROR) << "Network not initialized";
        return decoded_signals;
    }

    for (const auto& msg : network_->Messages()) {
        if (msg.Id() == can_id) {
            for (const auto& sig : msg.Signals()) {
                try {
                    double raw_value = sig.Decode(data);
                    double physical_value = sig.RawToPhys(raw_value);
                    
                    DBCDecodedValue decoded_value;
                    
                    // Check if this signal has enum mappings
                    bool has_enums = (signal_enums_.find(sig.Name()) != signal_enums_.end());
                    decoded_value.has_enums = has_enums;
                    
                    // Determine type based on signal properties
                    // Check if the physical value is an integer
                    if (std::floor(physical_value) == physical_value && 
                        physical_value >= std::numeric_limits<int64_t>::min() &&
                        physical_value <= std::numeric_limits<int64_t>::max()) {
                        // It's an integer
                        decoded_value.value = static_cast<int64_t>(physical_value);
                        VLOG(2) << "Decoded signal " << sig.Name() << " = " << static_cast<int64_t>(physical_value) << " (int)";
                    } else {
                        // It's a float
                        decoded_value.value = physical_value;
                        VLOG(2) << "Decoded signal " << sig.Name() << " = " << physical_value << " (float)";
                    }
                    
                    decoded_signals[sig.Name()] = std::move(decoded_value);
                } catch (const std::exception& e) {
                    LOG(WARNING) << "Failed to decode signal " << sig.Name() << ": " << e.what();
                }
            }
            break;
        }
    }
    
    return decoded_signals;
}

std::vector<DBCSignalUpdate> DBCParser::decode_message_as_updates(uint32_t can_id, const uint8_t* data, size_t length) const {
    std::vector<DBCSignalUpdate> updates;
    
    if (!network_) {
        LOG(ERROR) << "Network not initialized";
        return updates;
    }

    for (const auto& msg : network_->Messages()) {
        if (msg.Id() == can_id) {
            for (const auto& sig : msg.Signals()) {
                try {
                    double raw_value = sig.Decode(data);
                    double physical_value = sig.RawToPhys(raw_value);
                    
                    DBCSignalUpdate update;
                    update.dbc_signal_name = std::string_view(sig.Name());
                    
                    // Check if this signal has enum mappings
                    update.has_enums = (signal_enums_.find(sig.Name()) != signal_enums_.end());
                    
                    // Determine type based on signal properties
                    // Check if the physical value is an integer
                    if (std::floor(physical_value) == physical_value && 
                        physical_value >= std::numeric_limits<int64_t>::min() &&
                        physical_value <= std::numeric_limits<int64_t>::max()) {
                        // It's an integer
                        update.value = static_cast<int64_t>(physical_value);
                        VLOG(2) << "Decoded signal " << sig.Name() << " = " << static_cast<int64_t>(physical_value) << " (int)";
                    } else {
                        // It's a float
                        update.value = physical_value;
                        VLOG(2) << "Decoded signal " << sig.Name() << " = " << physical_value << " (float)";
                    }
                    
                    updates.push_back(std::move(update));
                } catch (const std::exception& e) {
                    LOG(WARNING) << "Failed to decode signal " << sig.Name() << ": " << e.what();
                }
            }
            break;
        }
    }
    
    return updates;
}

bool DBCParser::has_message(uint32_t can_id) const {
    if (!network_) return false;
    
    for (const auto& msg : network_->Messages()) {
        if (msg.Id() == can_id) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> DBCParser::get_signal_names(uint32_t can_id) const {
    std::vector<std::string> signal_names;
    
    if (!network_) return signal_names;
    
    for (const auto& msg : network_->Messages()) {
        if (msg.Id() == can_id) {
            for (const auto& sig : msg.Signals()) {
                signal_names.push_back(sig.Name());
            }
            break;
        }
    }
    
    return signal_names;
}

DBCParser::EnumMap DBCParser::get_signal_enums(const std::string& signal_name) const {
    auto it = signal_enums_.find(signal_name);
    if (it != signal_enums_.end()) {
        return it->second;
    }
    return {};
}

std::unordered_map<std::string, DBCParser::EnumMap> DBCParser::get_all_signal_enums() const {
    return signal_enums_;
}

std::optional<uint32_t> DBCParser::get_message_id_for_signal(const std::string& signal_name) const {
    if (!network_) {
        return std::nullopt;
    }
    
    for (const auto& msg : network_->Messages()) {
        for (const auto& sig : msg.Signals()) {
            if (sig.Name() == signal_name) {
                return msg.Id();
            }
        }
    }
    
    return std::nullopt;
}

} // namespace can_to_vss