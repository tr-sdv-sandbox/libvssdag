#pragma once

#include <string>

namespace vssdag {

// Information about where a signal comes from
struct SignalSource {
    std::string type;  // "dbc", "someip", "mqtt", etc.
    std::string name;  // Source-specific signal name
    
    // Convenience constructor
    SignalSource() = default;
    SignalSource(const std::string& t, const std::string& n) 
        : type(t), name(n) {}
    
    // Check if this is an input signal (has a source)
    bool is_input_signal() const {
        return !type.empty() && !name.empty();
    }
};

} // namespace vssdag