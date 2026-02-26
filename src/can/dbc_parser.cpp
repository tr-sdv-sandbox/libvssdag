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
        const uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
        size_t total_signals = 0;
        for (const auto& msg : network_->Messages()) {
            uint32_t msg_id = msg.Id() & CAN_EFF_MASK;
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

                VLOG(2) << "Signal " << msg.Name() << "." << sig.Name() << ": bits=" << bit_size
                        << ", invalid=" << std::hex << info.invalid_raw_value
                        << " (usable=" << info.can_use_invalid_pattern << ")"
                        << ", na=" << info.na_raw_value
                        << " (usable=" << info.can_use_na_pattern << ")"
                        << ", range=[" << std::dec << info.min_physical
                        << ", " << info.max_physical << "]";

                signal_info_[msg_id][sig.Name()] = std::move(info);
                ++total_signals;
            }
        }

        LOG(INFO) << "Successfully parsed DBC file: " << dbc_file_
                  << " with " << total_signals << " signals";
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

    auto updates = decode_message_as_updates(can_id, data, length);
    for (const auto& update : updates) {
        DBCDecodedValue decoded_value;
        decoded_value.value = update.value;
        decoded_value.status = update.status;
        decoded_value.has_enums = update.has_enums;
        
        decoded_signals[std::string(update.dbc_signal_name)] = std::move(decoded_value);
    }
    
    return decoded_signals;
}

std::optional<DBCSignalUpdate> DBCParser::decode_signal_as_update(const dbcppp::ISignal& sig, const std::string& msg_name, const uint8_t* data, uint32_t can_id_masked, std::optional<uint64_t> mux_value) const {
    try {
        uint64_t raw_value = sig.Decode(data);
        double physical_value = sig.RawToPhys(raw_value);

        DBCSignalUpdate update;
        update.dbc_signal_name = std::string_view(sig.Name());
        update.dbc_message_name = std::string_view(msg_name);

        // Look up pre-calculated signal info by message ID + signal name
        auto msg_info_it = signal_info_.find(can_id_masked);
        if (msg_info_it != signal_info_.end()) {
            auto info_it = msg_info_it->second.find(sig.Name());
            if (info_it != msg_info_it->second.end()) {
                const auto& info = info_it->second;
                update.has_enums = !info.enums.empty();
                update.status = info.check_status(raw_value, physical_value);
            }
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
                VLOG(2) << "Decoded " << (mux_value ? "muxed " : "") << "signal " << sig.Name() << " = " << static_cast<int64_t>(physical_value)
                        << " (int" << (mux_value ? ", mux=" + std::to_string(*mux_value) : "") << ", status=" << static_cast<int>(update.status) << ")";
        } else {
            // It's a float (has scaling or is a fractional value)
            update.value = physical_value;
            VLOG(2) << "Decoded " << (mux_value ? "muxed " : "") << "signal " << sig.Name() << " = " << physical_value
                    << " (float" << (mux_value ? ", mux=" + std::to_string(*mux_value) : "") << ", status=" << static_cast<int>(update.status) << ")";
        }

        return update;
    } catch (const std::exception& e) {
        LOG(WARNING) << "Failed to decode " << (mux_value ? "muxed " : "") << "signal " << sig.Name() << ": " << e.what();
        return std::nullopt;
    }
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
            // Group signals by muxed or not
            std::unordered_map<uint64_t, std::vector<std::reference_wrapper<const dbcppp::ISignal>>> muxed_signals;
            std::unordered_map<std::string, uint64_t> mux_values;
            
            for (const auto& sig : msg.Signals()) {
                if (sig.MultiplexerIndicator() == dbcppp::ISignal::EMultiplexer::MuxValue) {
                    muxed_signals[sig.MultiplexerSwitchValue()].push_back(std::cref(sig));
                } else {
                    // Decode non-muxed and mux switch signals immediately
                    if (sig.MultiplexerIndicator() == dbcppp::ISignal::EMultiplexer::MuxSwitch) {
                        try {
                            mux_values[sig.Name()] = sig.Decode(data);
                        } catch (const std::exception& e) {
                            LOG(WARNING) << "Failed to decode mux switch signal " << sig.Name() << ": " << e.what();
                            continue;
                        }
                    }
                    
                    auto update = decode_signal_as_update(sig, msg.Name(), data, can_id_masked);
                    if (update) {
                        updates.push_back(std::move(*update));
                    }
                }
            }
            
            // Decode multiplexed signals based on active mux values
            for (const auto& [mux_value, signals] : muxed_signals) {
                for (const auto& [mux_name, active_value] : mux_values) {
                    if (active_value == mux_value) {
                        for (const auto& sig_ref : signals) {
                            auto update = decode_signal_as_update(sig_ref.get(), msg.Name(), data, can_id_masked, mux_value);
                            if (update) {
                                updates.push_back(std::move(*update));
                            }
                        }
                        break;
                    }
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
    for (const auto& [msg_id, signals] : signal_info_) {
        auto it = signals.find(signal_name);
        if (it != signals.end()) {
            return it->second.enums;
        }
    }
    return {};
}

std::unordered_map<std::string, DBCParser::EnumMap> DBCParser::get_all_signal_enums() const {
    std::unordered_map<std::string, EnumMap> all_enums;
    for (const auto& [msg_id, signals] : signal_info_) {
        for (const auto& [name, info] : signals) {
            if (!info.enums.empty()) {
                all_enums[name] = info.enums;
            }
        }
    }
    return all_enums;
}

std::optional<uint32_t> DBCParser::get_message_id_for_signal(const std::string& message_name, const std::string& signal_name) const {
    if (!network_) {
        return std::nullopt;
    }
    
    for (const auto& msg : network_->Messages()) {
        if (msg.Name() == message_name) {
            for (const auto& sig : msg.Signals()) {
                if (sig.Name() == signal_name) {
                    const uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
                    return msg.Id() & CAN_EFF_MASK;
                }
            }
            break;
        }
    }
    
    return std::nullopt;
}

std::optional<std::string> DBCParser::get_enum_string(const std::string& signal_name, int64_t value) const {
    for (const auto& [msg_id, signals] : signal_info_) {
        auto it = signals.find(signal_name);
        if (it != signals.end()) {
            auto enum_it = it->second.reverse_enums.find(value);
            if (enum_it != it->second.reverse_enums.end()) {
                return enum_it->second;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace vssdag