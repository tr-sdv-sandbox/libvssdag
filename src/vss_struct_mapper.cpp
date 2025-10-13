#include "vssdag/vss_struct_mapper.h"
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>
#include <glog/logging.h>
#include <fstream>
#include <sstream>

namespace vssdag {

// StructBuffer Implementation
StructBuffer::StructBuffer(const StructType& type, const StructSignalMapping& mapping)
    : type_(type), mapping_(mapping), creation_time_(std::chrono::steady_clock::now()) {
    // Initialize field values map
    for (const auto& prop : type_.properties) {
        field_values_[prop.name] = FieldValue{};
    }
}

bool StructBuffer::update_field(const std::string& property_name, 
                               const std::variant<double, std::string, bool>& value) {
    auto it = field_values_.find(property_name);
    if (it == field_values_.end()) {
        LOG(WARNING) << "Unknown struct property: " << property_name;
        return false;
    }
    
    it->second.value = value;
    it->second.timestamp = std::chrono::steady_clock::now();
    it->second.is_set = true;
    
    VLOG(2) << "Updated struct field " << property_name << " in " << mapping_.vss_path;
    return true;
}

bool StructBuffer::is_complete() const {
    for (const auto& [name, field] : field_values_) {
        if (!field.is_set) {
            return false;
        }
    }
    return true;
}

bool StructBuffer::is_expired() const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time_).count();
    return age > mapping_.max_wait_ms;
}

std::optional<std::unordered_map<std::string, std::variant<double, std::string, bool>>> 
StructBuffer::get_struct_value() const {
    if (!is_complete() && mapping_.update_policy == StructUpdatePolicy::ATOMIC) {
        return std::nullopt;
    }
    
    std::unordered_map<std::string, std::variant<double, std::string, bool>> result;
    for (const auto& [name, field] : field_values_) {
        if (field.is_set) {
            result[name] = field.value;
        } else if (mapping_.update_policy == StructUpdatePolicy::PARTIAL_DEFAULT) {
            // Use default value if available
            const auto* prop = type_.get_property(name);
            if (prop && prop->default_value) {
                result[name] = *prop->default_value;
            }
        }
    }
    
    return result;
}

void StructBuffer::clear() {
    for (auto& [name, field] : field_values_) {
        field.is_set = false;
    }
    creation_time_ = std::chrono::steady_clock::now();
}

int StructBuffer::get_age_ms() const {
    auto now = std::chrono::steady_clock::now();
    auto oldest_time = creation_time_;
    
    for (const auto& [name, field] : field_values_) {
        if (field.is_set && field.timestamp < oldest_time) {
            oldest_time = field.timestamp;
        }
    }
    
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_time).count();
}

// VSSStructMapper Implementation
VSSStructMapper::VSSStructMapper() 
    : lua_mapper_(std::make_unique<LuaMapper>()) {
}

VSSStructMapper::~VSSStructMapper() = default;

bool VSSStructMapper::load_struct_types(const std::string& vss_spec_file) {
    try {
        std::ifstream file(vss_spec_file);
        if (!file) {
            LOG(ERROR) << "Failed to open VSS spec file: " << vss_spec_file;
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return parse_vss_struct_types(buffer.str());
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error loading struct types: " << e.what();
        return false;
    }
}

bool VSSStructMapper::parse_vss_struct_types(const std::string& vss_content) {
    try {
        YAML::Node root = YAML::Load(vss_content);
        
        // Iterate through all top-level nodes
        for (auto it = root.begin(); it != root.end(); ++it) {
            std::string node_name = it->first.as<std::string>();
            YAML::Node node = it->second;
            
            // Check if this is a struct type definition (e.g., "Types.Location")
            if (node_name.find("Types.") == 0 && node["type"] && 
                node["type"].as<std::string>() == "struct") {
                
                StructType struct_type;
                struct_type.type_path = node_name;
                struct_type.description = node["description"].as<std::string>("");
                
                // Now look for properties of this struct (e.g., "Types.Location.Latitude")
                std::string prefix = node_name + ".";
                for (auto prop_it = root.begin(); prop_it != root.end(); ++prop_it) {
                    std::string prop_full_name = prop_it->first.as<std::string>();
                    
                    // Check if this node is a property of our struct
                    if (prop_full_name.find(prefix) == 0) {
                        YAML::Node prop_node = prop_it->second;
                        
                        if (prop_node["type"] && prop_node["type"].as<std::string>() == "property") {
                            StructProperty prop;
                            // Extract property name (part after the last dot)
                            size_t last_dot = prop_full_name.rfind('.');
                            prop.name = (last_dot != std::string::npos) 
                                ? prop_full_name.substr(last_dot + 1)
                                : prop_full_name;
                            
                            prop.datatype = prop_node["datatype"].as<std::string>("");
                            prop.description = prop_node["description"].as<std::string>("");
                            prop.unit = prop_node["unit"].as<std::string>("");
                            
                            if (prop_node["min"]) prop.min = prop_node["min"].as<double>();
                            if (prop_node["max"]) prop.max = prop_node["max"].as<double>();
                            
                            if (prop_node["default"]) {
                                // Handle different default value types
                                if (prop.datatype == "boolean") {
                                    prop.default_value = prop_node["default"].as<bool>();
                                } else if (prop.datatype == "string") {
                                    prop.default_value = prop_node["default"].as<std::string>();
                                } else {
                                    prop.default_value = prop_node["default"].as<double>();
                                }
                            }
                            
                            struct_type.properties.push_back(prop);
                        }
                    }
                }
                
                if (!struct_type.properties.empty()) {
                    struct_types_[struct_type.type_path] = struct_type;
                    LOG(INFO) << "Loaded struct type: " << struct_type.type_path 
                             << " with " << struct_type.properties.size() << " properties";
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error parsing VSS struct types: " << e.what();
        return false;
    }
}

bool VSSStructMapper::load_struct_mappings(const std::string& mapping_file) {
    try {
        YAML::Node config = YAML::LoadFile(mapping_file);
        
        if (!config["struct_signals"]) {
            LOG(INFO) << "No struct signal mappings found";
            return true;
        }
        
        for (const auto& signal_node : config["struct_signals"]) {
            StructSignalMapping mapping;
            mapping.vss_path = signal_node["vss_signal"].as<std::string>();
            
            // Determine struct type from VSS path or explicit type
            if (signal_node["struct_type"]) {
                mapping.struct_type = signal_node["struct_type"].as<std::string>();
            }
            
            if (signal_node["interval_ms"]) {
                mapping.interval_ms = signal_node["interval_ms"].as<int>();
            }
            
            if (signal_node["max_wait_ms"]) {
                mapping.max_wait_ms = signal_node["max_wait_ms"].as<int>();
            }
            
            // Parse update policy
            if (signal_node["update_policy"]) {
                std::string policy = signal_node["update_policy"].as<std::string>();
                if (policy == "atomic") {
                    mapping.update_policy = StructUpdatePolicy::ATOMIC;
                } else if (policy == "partial_buffer") {
                    mapping.update_policy = StructUpdatePolicy::PARTIAL_BUFFER;
                } else if (policy == "partial_default") {
                    mapping.update_policy = StructUpdatePolicy::PARTIAL_DEFAULT;
                } else if (policy == "immediate") {
                    mapping.update_policy = StructUpdatePolicy::IMMEDIATE;
                }
            }
            
            // Parse property mappings
            if (signal_node["struct_mapping"]) {
                for (auto it = signal_node["struct_mapping"].begin(); 
                     it != signal_node["struct_mapping"].end(); ++it) {
                    StructPropertyMapping prop_mapping;
                    prop_mapping.property_path = it->first.as<std::string>();
                    
                    YAML::Node prop_node = it->second;
                    prop_mapping.can_signal = prop_node["can_signal"].as<std::string>();
                    
                    // Parse transform
                    if (prop_node["transform"]) {
                        if (prop_node["transform"]["math"]) {
                            CodeTransform ct;
                            ct.expression = prop_node["transform"]["math"].as<std::string>();
                            prop_mapping.transform = ct;
                        } else if (prop_node["transform"]["mapping"]) {
                            ValueMapping vm;
                            for (const auto& map_entry : prop_node["transform"]["mapping"]) {
                                vm.mappings[map_entry["from"].as<std::string>()] = 
                                    map_entry["to"].as<std::string>();
                            }
                            prop_mapping.transform = vm;
                        }
                    } else {
                        prop_mapping.transform = DirectMapping{};
                    }
                    
                    // Parse input signals for multi-signal transforms
                    if (prop_node["input_signals"]) {
                        for (const auto& sig : prop_node["input_signals"]) {
                            prop_mapping.input_signals.push_back(sig.as<std::string>());
                        }
                    }
                    
                    mapping.property_mappings.push_back(prop_mapping);
                    
                    // Index CAN signal to struct mapping
                    signal_to_struct_index_[prop_mapping.can_signal] = struct_mappings_.size();
                }
            }
            
            struct_mappings_.push_back(mapping);
            
            // Create buffer for this struct signal
            auto* struct_type = get_struct_type(mapping.struct_type);
            if (struct_type) {
                struct_buffers_.push_back(
                    std::make_unique<StructBuffer>(*struct_type, struct_mappings_.back()));
            } else {
                LOG(ERROR) << "Unknown struct type: " << mapping.struct_type;
                return false;
            }
            
            LOG(INFO) << "Loaded struct mapping for " << mapping.vss_path 
                     << " with " << mapping.property_mappings.size() << " properties";
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error loading struct mappings: " << e.what();
        return false;
    }
}

std::vector<VSSSignal> VSSStructMapper::process_struct_signals(
    const std::vector<std::pair<std::string, double>>& can_signals) {
    
    std::vector<VSSSignal> vss_signals;
    auto now = std::chrono::steady_clock::now();
    
    // Process each CAN signal
    for (const auto& [can_signal, value] : can_signals) {
        auto it = signal_to_struct_index_.find(can_signal);
        if (it == signal_to_struct_index_.end()) {
            continue;  // Not a struct signal
        }
        
        size_t struct_idx = it->second;
        auto& mapping = struct_mappings_[struct_idx];
        auto& buffer = struct_buffers_[struct_idx];
        
        // Find which property this signal maps to
        for (const auto& prop_mapping : mapping.property_mappings) {
            if (prop_mapping.can_signal == can_signal) {
                // Apply transformation
                auto transformed = apply_transform(value, prop_mapping.transform, can_signal);
                
                // Extract property name from path (e.g., "Types.Location.Latitude" -> "Latitude")
                size_t last_dot = prop_mapping.property_path.rfind('.');
                std::string prop_name = (last_dot != std::string::npos) 
                    ? prop_mapping.property_path.substr(last_dot + 1)
                    : prop_mapping.property_path;
                
                // Update buffer
                buffer->update_field(prop_name, transformed);
                
                VLOG(3) << "Updated " << prop_name << " in struct " << mapping.vss_path;
                break;
            }
        }
        
        // Check if we should emit the struct
        bool should_emit = false;
        
        switch (mapping.update_policy) {
            case StructUpdatePolicy::ATOMIC:
                should_emit = buffer->is_complete();
                break;
            case StructUpdatePolicy::PARTIAL_BUFFER:
                should_emit = buffer->is_complete() || buffer->is_expired();
                break;
            case StructUpdatePolicy::PARTIAL_DEFAULT:
                should_emit = true;  // Always emit with defaults
                break;
            case StructUpdatePolicy::IMMEDIATE:
                should_emit = true;  // Always emit whatever we have
                break;
        }
        
        // Check rate limiting
        if (should_emit) {
            auto& last_time = last_emission_times_[mapping.vss_path];
            auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_time).count();
            
            if (time_since_last >= mapping.interval_ms) {
                auto struct_map = buffer->get_struct_value();
                if (struct_map) {
                    // Convert old-style map to StructValue
                    auto struct_value = std::make_shared<vss::types::StructValue>(mapping.struct_type);
                    for (const auto& [field_name, field_val] : *struct_map) {
                        std::visit([&](auto&& val) {
                            struct_value->set_field(field_name, vss::types::Value(val));
                        }, field_val);
                    }

                    VSSSignal vss_signal;
                    vss_signal.path = mapping.vss_path;
                    vss_signal.qualified_value.value = struct_value;
                    vss_signal.qualified_value.quality = vss::types::SignalQuality::VALID;
                    vss_signal.qualified_value.timestamp = std::chrono::system_clock::now();

                    vss_signals.push_back(vss_signal);
                    last_time = now;

                    // Clear buffer after emission
                    buffer->clear();

                    LOG(INFO) << "Emitted struct signal: " << mapping.vss_path;
                }
            }
        }
    }
    
    // Check for expired buffers that should emit partial data
    for (size_t i = 0; i < struct_buffers_.size(); ++i) {
        auto& buffer = struct_buffers_[i];
        auto& mapping = struct_mappings_[i];
        
        if (buffer->is_expired() && 
            (mapping.update_policy == StructUpdatePolicy::PARTIAL_BUFFER ||
             mapping.update_policy == StructUpdatePolicy::PARTIAL_DEFAULT)) {
            
            auto struct_map = buffer->get_struct_value();
            if (struct_map) {
                // Convert old-style map to StructValue
                auto struct_value = std::make_shared<vss::types::StructValue>(mapping.struct_type);
                for (const auto& [field_name, field_val] : *struct_map) {
                    std::visit([&](auto&& val) {
                        struct_value->set_field(field_name, vss::types::Value(val));
                    }, field_val);
                }

                VSSSignal vss_signal;
                vss_signal.path = mapping.vss_path;
                vss_signal.qualified_value.value = struct_value;
                vss_signal.qualified_value.quality = vss::types::SignalQuality::VALID;
                vss_signal.qualified_value.timestamp = std::chrono::system_clock::now();

                vss_signals.push_back(vss_signal);
                buffer->clear();

                LOG(INFO) << "Emitted partial struct signal: " << mapping.vss_path;
            }
        }
    }
    
    return vss_signals;
}

const StructType* VSSStructMapper::get_struct_type(const std::string& type_path) const {
    auto it = struct_types_.find(type_path);
    return (it != struct_types_.end()) ? &it->second : nullptr;
}

bool VSSStructMapper::is_struct_signal(const std::string& can_signal) const {
    return signal_to_struct_index_.find(can_signal) != signal_to_struct_index_.end();
}

std::variant<double, std::string, bool> VSSStructMapper::apply_transform(
    double can_value, 
    const Transform& transform,
    const std::string& signal_name) {
    
    if (std::holds_alternative<DirectMapping>(transform)) {
        return can_value;
    } else if (auto* code = std::get_if<CodeTransform>(&transform)) {
        // Use Lua for math transformations
        std::string lua_code = "function transform_" + signal_name + "(x) return " + code->expression + " end";
        lua_mapper_->execute_lua_string(lua_code);
        
        // Call the transform function
        auto result = lua_mapper_->call_transform_function("transform_" + signal_name, can_value);
        if (result) {
            // Parse the result value as a number
            try {
                // Extract the value from qualified_value and convert to double
                if (auto* d = std::get_if<double>(&result->qualified_value.value)) {
                    return *d;
                } else if (auto* f = std::get_if<float>(&result->qualified_value.value)) {
                    return static_cast<double>(*f);
                } else if (auto* i = std::get_if<int64_t>(&result->qualified_value.value)) {
                    return static_cast<double>(*i);
                } else if (auto* str = std::get_if<std::string>(&result->qualified_value.value)) {
                    return std::stod(*str);
                }
                return can_value;
            } catch (...) {
                return can_value;
            }
        }
    } else if (auto* mapping = std::get_if<ValueMapping>(&transform)) {
        // Convert numeric value to string key for lookup
        std::string key = std::to_string(static_cast<int>(can_value));
        auto it = mapping->mappings.find(key);
        if (it != mapping->mappings.end()) {
            // Try to parse as number first, then bool, then string
            try {
                return std::stod(it->second);
            } catch (...) {
                if (it->second == "true") return true;
                if (it->second == "false") return false;
                return it->second;
            }
        }
    }
    
    return can_value;  // Default: return unchanged
}

std::string VSSStructMapper::format_struct_value(
    const std::unordered_map<std::string, std::variant<double, std::string, bool>>& struct_value) const {
    
    nlohmann::json json_obj;
    
    for (const auto& [name, value] : struct_value) {
        if (auto* d = std::get_if<double>(&value)) {
            json_obj[name] = *d;
        } else if (auto* s = std::get_if<std::string>(&value)) {
            json_obj[name] = *s;
        } else if (auto* b = std::get_if<bool>(&value)) {
            json_obj[name] = *b;
        }
    }
    
    return json_obj.dump();
}

} // namespace vssdag