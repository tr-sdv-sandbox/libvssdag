#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <variant>
#include <chrono>
#include <optional>
#include "signal_mapper.h"

namespace can_to_vss {

// Represents a property within a struct type
struct StructProperty {
    std::string name;           // Property name (e.g., "Latitude")
    std::string datatype;        // VSS datatype (e.g., "double")
    std::string description;
    std::optional<double> min;
    std::optional<double> max;
    std::string unit;
    std::optional<std::variant<double, std::string, bool>> default_value;
};

// Represents a complete struct type definition
struct StructType {
    std::string type_path;      // Full path (e.g., "Types.Location")
    std::string description;
    std::vector<StructProperty> properties;
    
    // Helper to get property by name
    const StructProperty* get_property(const std::string& name) const {
        for (const auto& prop : properties) {
            if (prop.name == name) return &prop;
        }
        return nullptr;
    }
};

// Mapping configuration for a single struct property
struct StructPropertyMapping {
    std::string property_path;   // e.g., "Types.Location.Latitude"
    std::string can_signal;       // Source CAN signal
    Transform transform;          // Transformation to apply
    std::vector<std::string> input_signals;  // For multi-signal transforms
};

// Update policy for struct signals
enum class StructUpdatePolicy {
    ATOMIC,           // All fields must be present
    PARTIAL_BUFFER,   // Buffer partial updates until complete
    PARTIAL_DEFAULT,  // Use defaults for missing fields
    IMMEDIATE        // Emit partial updates immediately
};

// Complete struct mapping configuration
struct StructSignalMapping {
    std::string vss_path;        // e.g., "Vehicle.CurrentLocation"
    std::string struct_type;     // e.g., "Types.Location"
    std::vector<StructPropertyMapping> property_mappings;
    StructUpdatePolicy update_policy = StructUpdatePolicy::ATOMIC;
    int interval_ms = 100;
    int max_wait_ms = 200;       // Max time to wait for all fields
    
    // For array of structs
    bool is_array = false;
    int array_size = 0;          // 0 = dynamic array
    int array_index = -1;        // -1 = not an array element
};

// Buffer for collecting struct field values
class StructBuffer {
public:
    StructBuffer(const StructType& type, const StructSignalMapping& mapping);
    
    // Update a field value
    bool update_field(const std::string& property_name, 
                     const std::variant<double, std::string, bool>& value);
    
    // Check if all required fields are present
    bool is_complete() const;
    
    // Check if buffer has expired
    bool is_expired() const;
    
    // Get the complete struct value (if ready)
    std::optional<std::unordered_map<std::string, std::variant<double, std::string, bool>>> 
        get_struct_value() const;
    
    // Clear the buffer
    void clear();
    
    // Get age of oldest field in milliseconds
    int get_age_ms() const;

private:
    const StructType& type_;
    const StructSignalMapping& mapping_;
    
    struct FieldValue {
        std::variant<double, std::string, bool> value;
        std::chrono::steady_clock::time_point timestamp;
        bool is_set = false;
    };
    
    std::unordered_map<std::string, FieldValue> field_values_;
    std::chrono::steady_clock::time_point creation_time_;
};

// Main struct mapper class
class VSSStructMapper {
public:
    VSSStructMapper();
    ~VSSStructMapper();
    
    // Load struct type definitions from VSS spec
    bool load_struct_types(const std::string& vss_spec_file);
    
    // Load struct mapping configuration
    bool load_struct_mappings(const std::string& mapping_file);
    
    // Process CAN signals and update struct buffers
    std::vector<VSSSignal> process_struct_signals(
        const std::vector<std::pair<std::string, double>>& can_signals);
    
    // Get struct type definition
    const StructType* get_struct_type(const std::string& type_path) const;
    
    // Check if a signal is part of a struct
    bool is_struct_signal(const std::string& can_signal) const;
    
    // Get all struct mappings
    const std::vector<StructSignalMapping>& get_struct_mappings() const { 
        return struct_mappings_; 
    }

private:
    // Struct type definitions (from VSS spec)
    std::unordered_map<std::string, StructType> struct_types_;
    
    // Struct signal mappings
    std::vector<StructSignalMapping> struct_mappings_;
    
    // CAN signal to struct mapping index
    std::unordered_map<std::string, size_t> signal_to_struct_index_;
    
    // Buffers for collecting struct values
    std::vector<std::unique_ptr<StructBuffer>> struct_buffers_;
    
    // Lua mapper for transformations
    std::unique_ptr<LuaMapper> lua_mapper_;
    
    // Last emission times for rate limiting
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_emission_times_;
    
    // Helper methods
    bool parse_vss_struct_types(const std::string& vss_content);
    bool parse_struct_mapping_yaml(const std::string& yaml_content);
    std::variant<double, std::string, bool> apply_transform(
        double can_value, 
        const Transform& transform,
        const std::string& signal_name);
    
    // Format struct value as JSON for VSS
    std::string format_struct_value(
        const std::unordered_map<std::string, std::variant<double, std::string, bool>>& struct_value) const;
};

} // namespace can_to_vss