#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <functional>
#include <variant>
#include <yaml-cpp/yaml.h>
//#include "base_types.h"

namespace vssdag {

// Signal status for invalid/unavailable detection
enum class SignalStatus {
    Valid,        // Normal, valid signal value
    Invalid,      // Error state (sensor failure, out of range, etc.)
    NotAvailable  // Not equipped, not ready, no data available
};

// Signal update with type information preserved
struct SignalUpdate {
    std::string signal_name;  // Exported signal name
    std::variant<int64_t, double, std::string> value;  // Typed value (may be dummy when status != Valid)
    std::chrono::steady_clock::time_point timestamp;
    SignalStatus status = SignalStatus::Valid;  // Signal validity status
};

class ISignalSource {
public:
    virtual ~ISignalSource() = default;
    
    virtual bool initialize() = 0;
    
    // Non-blocking poll for new signal updates
    // Returns empty vector if no updates available
    virtual std::vector<SignalUpdate> poll() = 0;    
    
    // Get list of signals this source exports
    virtual std::vector<std::string> get_exported_signals() const = 0; 
    };
}
    

