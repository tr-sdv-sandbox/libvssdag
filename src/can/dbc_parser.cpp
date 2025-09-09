#include "vssdag/can/dbc_parser.h"
#include <dbcppp/Network.h>
#include <glog/logging.h>
#include <fstream>
#include <cmath>
#include <limits>

namespace vssdag {

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
        
        // Extract signal information and pre-calculate invalid/NA patterns
        signal_info_.clear();
        for (const auto& msg : network_->Messages()) {
            for (const auto& sig : msg.Signals()) {
                SignalInfo info;
                
                // Extract enum mappings
                for (const auto& value_desc : sig.ValueEncodingDescriptions()) {
                    info.enums[value_desc.Description()] = value_desc.Value();
                    info.reverse_enums[value_desc.Value()] = value_desc.Description();
                    VLOG(2) << "Signal " << sig.Name() << " enum: " 
                            << value_desc.Value() << " = " << value_desc.Description();
                }
                
                // Pre-calculate invalid/NA patterns
                uint64_t bit_size = sig.BitSize();
                uint64_t max_possible_raw = (bit_size >= 64) ? UINT64_MAX : ((1ULL << bit_size) - 1);
                
                info.invalid_raw_value = max_possible_raw;
                info.na_raw_value = max_possible_raw - 1;
                info.min_physical = sig.Minimum();
                info.max_physical = sig.Maximum();
                
                // Check if invalid pattern is usable (outside valid range)
                double physical_invalid = sig.RawToPhys(info.invalid_raw_value);
                info.can_use_invalid_pattern = (physical_invalid < info.min_physical || 
                                               physical_invalid > info.max_physical);
                
                // Check if NA pattern is usable (outside valid range)
                double physical_na = sig.RawToPhys(info.na_raw_value);
                info.can_use_na_pattern = (physical_na < info.min_physical || 
                                          physical_na > info.max_physical);
                
                VLOG(2) << "Signal " << sig.Name() << ": bits=" << bit_size 
                        << ", invalid=" << std::hex << info.invalid_raw_value 
                        << " (usable=" << info.can_use_invalid_pattern << ")"
                        << ", na=" << info.na_raw_value 
                        << " (usable=" << info.can_use_na_pattern << ")"
                        << ", range=[" << std::dec << info.min_physical 
                        << ", " << info.max_physical << "]";
                
                signal_info_[sig.Name()] = std::move(info);
            }
        }
        
        LOG(INFO) << "Successfully parsed DBC file: " << dbc_file_ 
                  << " with " << signal_info_.size() << " signals";
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

    // Always strip extended frame flag for comparison
    const uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
    uint32_t can_id_masked = can_id & CAN_EFF_MASK;

    for (const auto& msg : network_->Messages()) {
        if ((msg.Id() & CAN_EFF_MASK) == can_id_masked) {
            for (const auto& sig : msg.Signals()) {
                try {
                    uint64_t raw_value = sig.Decode(data);
                    double physical_value = sig.RawToPhys(raw_value);
                    
                    DBCDecodedValue decoded_value;
                    
                    // Look up pre-calculated signal info
                    auto info_it = signal_info_.find(sig.Name());
                    if (info_it != signal_info_.end()) {
                        const auto& info = info_it->second;
                        decoded_value.has_enums = !info.enums.empty();
                        decoded_value.status = info.check_status(raw_value, physical_value);
                    } else {
                        // Shouldn't happen if parse() was successful
                        decoded_value.has_enums = false;
                        decoded_value.status = SignalStatus::Valid;
                    }
                    
                    // Determine type based on signal properties
                    // If the signal has scaling (factor != 1.0 or offset != 0), treat as double
                    // Otherwise check if it can be represented as an integer
                    if ((sig.Factor() == 1.0 && sig.Offset() == 0.0) &&
                            std::floor(physical_value) == physical_value && 
                            physical_value >= std::numeric_limits<int64_t>::min() &&
                            physical_value <= std::numeric_limits<int64_t>::max()) {
                            // It's an integer signal with no scaling
                            decoded_value.value = static_cast<int64_t>(physical_value);
                            VLOG(2) << "Decoded signal " << sig.Name() << " = " << static_cast<int64_t>(physical_value) 
                                    << " (int, status=" << static_cast<int>(decoded_value.status) << ")";
                    } else {
                        // It's a float (has scaling or is a fractional value)
                        decoded_value.value = physical_value;
                        VLOG(2) << "Decoded signal " << sig.Name() << " = " << physical_value 
                                << " (float, status=" << static_cast<int>(decoded_value.status) << ")";
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

    // Always strip extended frame flag for comparison
    const uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
    uint32_t can_id_masked = can_id & CAN_EFF_MASK;

    for (const auto& msg : network_->Messages()) {
        if ((msg.Id() & CAN_EFF_MASK) == can_id_masked) {
            for (const auto& sig : msg.Signals()) {
                try {
                    uint64_t raw_value = sig.Decode(data);
                    double physical_value = sig.RawToPhys(raw_value);
                    
                    DBCSignalUpdate update;
                    update.dbc_signal_name = std::string_view(sig.Name());
                    
                    // Look up pre-calculated signal info
                    auto info_it = signal_info_.find(sig.Name());
                    if (info_it != signal_info_.end()) {
                        const auto& info = info_it->second;
                        update.has_enums = !info.enums.empty();
                        update.status = info.check_status(raw_value, physical_value);
                    } else {
                        // Shouldn't happen if parse() was successful
                        update.has_enums = false;
                        update.status = SignalStatus::Valid;
                    }
                    
                    // Determine type based on signal properties
                    // If the signal has scaling (factor != 1.0 or offset != 0), treat as double
                    // Otherwise check if it can be represented as an integer
                    if ((sig.Factor() == 1.0 && sig.Offset() == 0.0) &&
                            std::floor(physical_value) == physical_value && 
                            physical_value >= std::numeric_limits<int64_t>::min() &&
                            physical_value <= std::numeric_limits<int64_t>::max()) {
                            // It's an integer signal with no scaling
                            update.value = static_cast<int64_t>(physical_value);
                            VLOG(2) << "Decoded signal " << sig.Name() << " = " << static_cast<int64_t>(physical_value) 
                                    << " (int, status=" << static_cast<int>(update.status) << ")";
                    } else {
                        // It's a float (has scaling or is a fractional value)
                        update.value = physical_value;
                        VLOG(2) << "Decoded signal " << sig.Name() << " = " << physical_value 
                                << " (float, status=" << static_cast<int>(update.status) << ")";
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
    auto it = signal_info_.find(signal_name);
    if (it != signal_info_.end()) {
        return it->second.enums;
    }
    return {};
}

std::unordered_map<std::string, DBCParser::EnumMap> DBCParser::get_all_signal_enums() const {
    std::unordered_map<std::string, EnumMap> all_enums;
    for (const auto& [name, info] : signal_info_) {
        if (!info.enums.empty()) {
            all_enums[name] = info.enums;
        }
    }
    return all_enums;
}

std::optional<uint32_t> DBCParser::get_message_id_for_signal(const std::string& signal_name) const {
    if (!network_) {
        return std::nullopt;
    }
    
    for (const auto& msg : network_->Messages()) {
        for (const auto& sig : msg.Signals()) {
            if (sig.Name() == signal_name) {
                // Return the ID with extended flag stripped
                const uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
                return msg.Id() & CAN_EFF_MASK;
            }
        }
    }
    
    return std::nullopt;
}

std::optional<std::string> DBCParser::get_enum_string(const std::string& signal_name, int64_t value) const {
    auto it = signal_info_.find(signal_name);
    if (it == signal_info_.end()) {
        return std::nullopt;
    }
    
    auto enum_it = it->second.reverse_enums.find(value);
    if (enum_it == it->second.reverse_enums.end()) {
        return std::nullopt;
    }
    
    return enum_it->second;
}

} // namespace vssdag