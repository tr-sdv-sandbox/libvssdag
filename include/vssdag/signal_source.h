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
#include <vss/types/quality.hpp>
#include <vss/types/value.hpp>
//#include "base_types.h"

namespace vssdag {

// Signal update with type information preserved
struct SignalUpdate {
    std::string signal_name;  // Exported signal name
    vss::types::Value value;  // VSS typed value
    std::chrono::steady_clock::time_point timestamp;
    vss::types::SignalQuality status = vss::types::SignalQuality::VALID;  // Signal validity status
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
    

