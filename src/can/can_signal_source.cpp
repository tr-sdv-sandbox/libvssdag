#include "libVSSDAG/can/can_signal_source.h"
#include <glog/logging.h>

namespace vssdag {

CANSignalSource::CANSignalSource(const std::string& interface_name,
                                 const std::string& dbc_file_path,
                                 const std::unordered_map<std::string, can_to_vss::SignalMapping>& mappings)
    : interface_name_(interface_name)
    , dbc_file_path_(dbc_file_path)
    , mappings_(mappings) {
}

CANSignalSource::~CANSignalSource() {
    stop();
}

bool CANSignalSource::initialize() {
    // Parse DBC file
    dbc_parser_ = std::make_unique<can_to_vss::DBCParser>(dbc_file_path_);
    if (!dbc_parser_->parse()) {
        LOG(ERROR) << "Failed to parse DBC file: " << dbc_file_path_;
        return false;
    }
    
    // Extract DBC signals from mappings (where source.type == "dbc")
    for (const auto& [signal_name, mapping] : mappings_) {
        if (mapping.source.type == "dbc") {
            dbc_signal_names_.push_back(mapping.source.name);
            dbc_to_signal_name_[mapping.source.name] = signal_name;
        }
    }
    
    // Build set of required CAN IDs from DBC signal names
    for (const auto& dbc_signal_name : dbc_signal_names_) {
        auto can_id = dbc_parser_->get_message_id_for_signal(dbc_signal_name);
        if (can_id.has_value()) {
            required_can_ids_.insert(can_id.value());
            VLOG(1) << "DBC signal " << dbc_signal_name << " is in CAN message ID: 0x" 
                    << std::hex << can_id.value();
        } else {
            LOG(WARNING) << "DBC signal " << dbc_signal_name << " not found in DBC file";
        }
    }
    
    if (required_can_ids_.empty()) {
        LOG(WARNING) << "No valid CAN message IDs found for requested signals";
        return true; // Not an error, just no signals to monitor
    }
    
    LOG(INFO) << "CANSignalSource monitoring " << required_can_ids_.size() 
              << " CAN message IDs for " << dbc_signal_names_.size() << " DBC signals";
    
    // Create CAN reader
    can_reader_ = std::make_unique<can_to_vss::SocketCANReader>();
    if (!can_reader_->open(interface_name_)) {
        LOG(ERROR) << "Failed to open CAN interface: " << interface_name_;
        return false;
    }
    
    // Set up frame handler
    can_reader_->set_frame_handler([this](const can_to_vss::CANFrame& frame) {
        handle_can_frame(frame);
    });
    
    // Start reader thread
    running_ = true;
    reader_thread_ = std::make_unique<std::thread>([this]() {
        can_reader_->read_loop();
    });
    
    return true;
}

void CANSignalSource::handle_can_frame(const can_to_vss::CANFrame& frame) {
    // Quick check if we care about this CAN ID
    if (required_can_ids_.find(frame.id) == required_can_ids_.end()) {
        return;
    }
    
    VLOG(3) << "Processing CAN frame ID: 0x" << std::hex << frame.id;
    
    // Decode the frame directly to signal updates
    auto dbc_updates = dbc_parser_->decode_message_as_updates(
        frame.id, frame.data.data(), frame.data.size());
    
    // Convert to SignalUpdate and enqueue (only the signals we care about)
    auto timestamp = std::chrono::steady_clock::now();
    for (const auto& dbc_update : dbc_updates) {
        // Check if this DBC signal is one we need
        // Note: In C++17 we need to construct a string for the lookup
        auto it = dbc_to_signal_name_.find(std::string(dbc_update.dbc_signal_name));
        if (it != dbc_to_signal_name_.end()) {
            // Use our signal name (not the DBC name) in the update
            SignalUpdate update{it->second, dbc_update.value, timestamp};
            signal_queue_.enqueue(std::move(update));
            
            // Log with type info
            std::visit([&](auto&& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    VLOG(3) << "Enqueued signal: " << it->second << " (DBC: " << dbc_update.dbc_signal_name << ") = " << val << " (int)";
                } else if constexpr (std::is_same_v<T, double>) {
                    VLOG(3) << "Enqueued signal: " << it->second << " (DBC: " << dbc_update.dbc_signal_name << ") = " << val << " (double)";
                } else {
                    VLOG(3) << "Enqueued signal: " << it->second << " (DBC: " << dbc_update.dbc_signal_name << ") = " << val << " (string)";
                }
            }, dbc_update.value);
        }
    }
}

std::vector<SignalUpdate> CANSignalSource::poll() {
    std::vector<SignalUpdate> updates;
    SignalUpdate update;
    
    // Drain the queue up to a reasonable batch size
    const size_t max_batch_size = 100;
    while (updates.size() < max_batch_size && signal_queue_.try_dequeue(update)) {
        updates.push_back(std::move(update));
    }
    
    if (!updates.empty()) {
        VLOG(2) << "CANSignalSource::poll() returning " << updates.size() << " updates";
    }
    
    return updates;
}

std::vector<std::string> CANSignalSource::get_exported_signals() const {
    std::vector<std::string> signals;
    for (const auto& [signal_name, mapping] : mappings_) {
        if (mapping.source.type == "dbc") {
            signals.push_back(signal_name);
        }
    }
    return signals;
}

void CANSignalSource::stop() {
    if (running_) {
        running_ = false;
        if (can_reader_) {
            can_reader_->stop();
        }
        if (reader_thread_ && reader_thread_->joinable()) {
            reader_thread_->join();
        }
    }
}

} // namespace vssdag