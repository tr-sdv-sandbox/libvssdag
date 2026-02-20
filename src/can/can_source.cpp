#include "vssdag/can/can_source.h"
#include "vssdag/can/avtp_can_reader.h"
#include <glog/logging.h>

namespace vssdag {

CANSignalSource::CANSignalSource(CANTransport transport,
                                 const std::string& interface_name,
                                 const std::string& dbc_file_path,
                                 const std::unordered_map<std::string, SignalMapping>& mappings)
    : transport_(transport)
    , interface_name_(interface_name)
    , dbc_file_path_(dbc_file_path)
    , mappings_(mappings) {
}

CANSignalSource::CANSignalSource(std::unique_ptr<CANReader> can_reader,
                                 const std::string& dbc_file_path,
                                 const std::unordered_map<std::string, SignalMapping>& mappings)
    : can_reader_(std::move(can_reader))
    , dbc_file_path_(dbc_file_path)
    , mappings_(mappings) {
}

CANSignalSource::~CANSignalSource() {
    stop();
}

bool CANSignalSource::initialize() {
    // Parse DBC file
    dbc_parser_ = std::make_unique<DBCParser>(dbc_file_path_);
    if (!dbc_parser_->parse()) {
        LOG(ERROR) << "Failed to parse DBC file: " << dbc_file_path_;
        return false;
    }
    
    // Extract DBC signals from mappings and resolve to CAN IDs.
    // Source name must be in "Message.Signal" format.
    size_t signal_count = 0;
    for (const auto& [vss_name, mapping] : mappings_) {
        if (mapping.source.type != "dbc") continue;

        auto dot_pos = mapping.source.name.find('.');
        if (dot_pos == std::string::npos) {
            LOG(ERROR) << "DBC source name must be in 'Message.Signal' format, got: "
                       << mapping.source.name;
            return false;
        }

        std::string message_name = mapping.source.name.substr(0, dot_pos);
        std::string dbc_signal = mapping.source.name.substr(dot_pos + 1);

        auto can_id = dbc_parser_->get_message_id_for_signal(message_name, dbc_signal);
        if (!can_id.has_value()) {
            LOG(WARNING) << "DBC signal " << mapping.source.name << " not found in DBC file";
            continue;
        }

        can_id_signal_map_[can_id.value()][dbc_signal] = vss_name;
        ++signal_count;
        VLOG(1) << "DBC signal " << mapping.source.name << " is in CAN message ID: 0x"
                << std::hex << can_id.value();
    }

    if (can_id_signal_map_.empty()) {
        LOG(WARNING) << "No valid CAN message IDs found for requested signals";
        return true;
    }

    LOG(INFO) << "CANSignalSource monitoring " << can_id_signal_map_.size()
              << " CAN message IDs for " << signal_count << " DBC signals";

    // Create CAN reader based on transport type if not provided
    if (!can_reader_) {
        switch (transport_) {
            case CANTransport::SOCKETCAN:
                can_reader_ = std::make_unique<SocketCANReader>();
                break;
            case CANTransport::AVTP:
                can_reader_ = std::make_unique<AVTPCanReader>();
                break;
        }
    }

    if (!can_reader_->open(interface_name_)) {
        LOG(ERROR) << "Failed to open interface: " << interface_name_
                   << " (transport: " << (transport_ == CANTransport::AVTP ? "AVTP" : "SocketCAN") << ")";
        return false;
    }
    
    // Set up frame handler
    can_reader_->set_frame_handler([this](const CANFrame& frame) {
        handle_can_frame(frame);
    });
    
    // Start reader thread
    running_ = true;
    reader_thread_ = std::make_unique<std::thread>([this]() {
        can_reader_->read_loop();
    });
    
    return true;
}

void CANSignalSource::handle_can_frame(const CANFrame& frame) {
    // Fast lookup by CAN ID — no work if we don't care about this message
    auto msg_it = can_id_signal_map_.find(frame.id);
    if (msg_it == can_id_signal_map_.end()) {
        return;
    }

    VLOG(3) << "Processing CAN frame ID: 0x" << std::hex << frame.id;

    auto dbc_updates = dbc_parser_->decode_message_as_updates(
        frame.id, frame.data.data(), frame.data.size());

    auto timestamp = std::chrono::steady_clock::now();
    for (const auto& dbc_update : dbc_updates) {
        // Lookup by DBC signal name within this CAN ID
        auto sig_it = msg_it->second.find(std::string(dbc_update.dbc_signal_name));
        if (sig_it == msg_it->second.end()) continue;

        SignalUpdate update{sig_it->second, dbc_update.value, timestamp, dbc_update.status};
        signal_queue_.enqueue(std::move(update));

        const char* status_str = (dbc_update.status == vss::types::SignalQuality::VALID) ? "valid" :
                                (dbc_update.status == vss::types::SignalQuality::INVALID) ? "invalid" : "not_available";
        VLOG(3) << "Enqueued signal: " << sig_it->second << " (DBC: "
                << dbc_update.dbc_message_name << "." << dbc_update.dbc_signal_name
                << ") = " << VSSTypeHelper::to_string(dbc_update.value) << " (" << status_str << ")";
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