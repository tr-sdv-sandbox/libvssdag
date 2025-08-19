#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <variant>
#include <chrono>
#include "lua_mapper.h"
#include "can/dbc_parser.h"

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
    std::string vss_path;
    std::string datatype;
    int interval_ms;
    Transform transform;
    
    // DAG support
    std::string provides;  // Value name this signal provides (optional)
    std::vector<std::string> depends_on;  // Value names this depends on (optional)
    
    // Update triggering
    UpdateTrigger update_trigger = UpdateTrigger::ON_DEPENDENCY;
    
    // Struct support (VSS 4.0)
    std::string struct_type;  // e.g., "Types.Location" (empty if not a struct)
    std::string struct_field; // e.g., "Latitude" (field within the struct)
    bool is_struct = false;   // Quick check flag
};

class SignalMapper {
public:
    SignalMapper();
    ~SignalMapper();
    
    // Load YAML mapping file with DBC enum validation
    bool load_mapping_file(const std::string& yaml_file, const DBCParser* dbc_parser = nullptr);
    
    // Process CAN signals and return VSS signals
    std::vector<VSSSignal> process_signals(const std::vector<std::pair<std::string, double>>& can_signals);
    
    // Get all mapped CAN signal names
    std::vector<std::string> get_mapped_signals() const;
    
    // Check if a signal has a mapping
    bool has_mapping(const std::string& signal_name) const {
        return signal_mappings_.find(signal_name) != signal_mappings_.end();
    }

private:
    std::unique_ptr<LuaMapper> lua_mapper_;
    std::unordered_map<std::string, SignalMapping> signal_mappings_;  // Key: DBC signal name
    
    // Track last output time for each VSS path
    struct OutputTiming {
        std::chrono::steady_clock::time_point last_output;
        std::string last_value;  // To detect changes
    };
    std::unordered_map<std::string, OutputTiming> output_timing_;  // Key: VSS path
    
    // Struct buffer for collecting struct fields
    struct StructBuffer {
        std::unordered_map<std::string, std::string> field_values;  // field_name -> value
        std::unordered_map<std::string, bool> field_set;            // field_name -> is_set
        std::chrono::steady_clock::time_point creation_time;
        std::string struct_type;
        
        bool is_complete(const std::vector<std::string>& required_fields) const {
            for (const auto& field : required_fields) {
                if (field_set.find(field) == field_set.end() || !field_set.at(field)) {
                    return false;
                }
            }
            return true;
        }
        
        void clear() {
            field_values.clear();
            field_set.clear();
            creation_time = std::chrono::steady_clock::now();
        }
    };
    std::unordered_map<std::string, StructBuffer> struct_buffers_;  // Key: VSS path
    
    // Generate Lua transform function for a signal
    void generate_lua_transform(const std::string& signal_name, const SignalMapping& mapping);
    
    // Generate Lua code for different transform types
    std::string generate_direct_lua(const std::string& signal_name, const SignalMapping& mapping);
    std::string generate_code_lua(const std::string& signal_name, const SignalMapping& mapping, const CodeTransform& transform);
    std::string generate_mapping_lua(const std::string& signal_name, const SignalMapping& mapping, const ValueMapping& transform);
    
    // Convert enum string to value using DBC information
    std::string resolve_enum_value(const std::string& signal_name, const std::string& enum_string, 
                                   const DBCParser* dbc_parser) const;
};

} // namespace can_to_vss