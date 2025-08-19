#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include "signal_source_info.h"

namespace can_to_vss {

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

enum class UpdateTrigger {
    ON_DEPENDENCY,  // Only when dependencies update (default)
    PERIODIC,       // Every interval_ms regardless of dependencies
    BOTH           // On dependency update OR periodic
};

struct SignalMapping {
    std::string datatype;
    int interval_ms;
    Transform transform;
    
    // Source information (for input signals)
    SignalSource source;
    
    // DAG support
    std::vector<std::string> depends_on;  // Signal names this depends on
    
    // Update triggering
    UpdateTrigger update_trigger = UpdateTrigger::ON_DEPENDENCY;
    
    // Struct support (VSS 4.0)
    std::string struct_type;  // e.g., "Types.Location" (empty if not a struct)
    std::string struct_field; // e.g., "Latitude" (field within the struct)
    bool is_struct = false;   // Quick check flag
};

} // namespace can_to_vss