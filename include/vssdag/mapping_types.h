#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include "vssdag/signal_source_info.h"
#include "vssdag/vss_types.h"

namespace vssdag {

// Types of transformations
struct DirectMapping {
    // No transformation needed
};

struct CodeTransform {
    std::string expression;  // Lua code (single or multi-line)
};

struct ValueMapping {
    std::unordered_map<std::string, std::string> mappings;
};

using Transform = std::variant<DirectMapping, CodeTransform, ValueMapping>;

struct SignalMapping {
    ValueType datatype = ValueType::UNSPECIFIED;  // Default to unspecified, must be explicitly set
    Transform transform = DirectMapping{};  // Default to direct mapping

    // Source information (for input signals)
    SignalSource source;

    // DAG support
    std::vector<std::string> depends_on;  // Signal names this depends on

    // Output rate control
    int min_interval_ms = 0;      // Minimum time between emissions (rate limit/downsample)
                                  // 0 = no limit, emit as fast as changes occur
    int max_interval_ms = 10000;  // Maximum time between emissions (heartbeat for late-joiners)
                                  // 0 = disabled, only emit on change
                                  // Default 10s ensures eventual consistency

    // Change detection
    double change_threshold = 0;  // Minimum change to trigger emission (deadband)
                                  // 0 = any change triggers emission
                                  // For numeric signals: absolute delta
                                  // For booleans/strings: ignored (any change emits)

    // Processing control (for derived signals)
    int eval_interval_ms = 0;     // Re-evaluate transform at this interval even if deps unchanged
                                  // 0 = only evaluate when dependencies change
                                  // Useful for time-based transforms (derivative, decay, etc.)

    // Struct support (VSS 4.0)
    std::string struct_type;  // e.g., "Types.Location" (empty if not a struct)
    std::string struct_field; // e.g., "Latitude" (field within the struct)
    bool is_struct = false;   // Quick check flag
};

} // namespace vssdag